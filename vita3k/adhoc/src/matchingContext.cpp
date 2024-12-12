// Vita3K emulator project
// Copyright (C) 2023 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#include "util/log.h"
#include <adhoc/state.h>

#include <emuenv/state.h>
#include <kernel/state.h>
#include <net/types.h>
#include <thread>
#include <util/lock_and_find.h>
#include <util/types.h>

#include <cstring>

#include "../SceNet/SceNet.h"

#include <util/tracy.h>
TRACY_MODULE_NAME(SceNetAdhocMatching);

int SceNetAdhocMatchingContext::initialize(SceNetAdhocMatchingMode mode, int maxnum, SceUShort16 port, int rxbuflen, unsigned int helloInterval, unsigned int keepaliveInterval, int retryCount, unsigned int rexmtInterval, Ptr<void> handlerAddr) {
    this->mode = mode;

    // Children have 2 peers max (parent and itself)
    this->maxnum = 2;
    if (this->mode == SCE_NET_ADHOC_MATCHING_MODE_PARENT)
        this->maxnum = maxnum;

    this->port = port;
    this->rxbuflen = rxbuflen;
    this->rxbuf = new char[this->rxbuflen]; // Reserve space in adhoc

    if (this->rxbuf == nullptr) {
        return SCE_NET_ADHOC_MATCHING_ERROR_NO_SPACE;
    }

    this->helloInterval = helloInterval;
    this->keepAliveInterval = keepaliveInterval;
    this->retryCount = retryCount;
    this->rexmtInterval = rexmtInterval;

    this->shouldHelloReqBeProcessed = false;
    this->helloOptionFlag = 1;
    this->targetList = nullptr;

    this->handler = handlerAddr.address();
    return SCE_NET_ADHOC_MATCHING_OK;
}

int SceNetAdhocMatchingContext::start(EmuEnvState &emuenv, SceUID thread_id, int threadPriority, int threadStackSize, int threadCpuAffinityMask, SceSize helloOptlen, char *helloOpt) {
    int result = initializeSendSocket(emuenv, thread_id);
    if (result != SCE_NET_ADHOC_MATCHING_OK)
        return result;

    result = initializeEventHandler(emuenv, thread_id, threadPriority, threadStackSize, threadCpuAffinityMask);
    if (result != SCE_NET_ADHOC_MATCHING_OK) {
        closeSendSocket(emuenv, thread_id);
        return result;
    }

    result = initializeInputThread(emuenv, thread_id, threadPriority, 0x1000, threadCpuAffinityMask);
    if (result != SCE_NET_ADHOC_MATCHING_OK) {
        closeEventHandler();
        closeSendSocket(emuenv, thread_id);
        return result;
    }

    result = getCalloutSyncing().initializeCalloutThread(emuenv, thread_id, getId(), threadPriority, 0x1000, threadCpuAffinityMask);
    if (result != SCE_NET_ADHOC_MATCHING_OK) {
        closeInputThread(emuenv, thread_id);
        closeEventHandler();
        closeSendSocket(emuenv, thread_id);
        return result;
    }

    if (getMode() == SCE_NET_ADHOC_MATCHING_MODE_PARENT || getMode() == SCE_NET_ADHOC_MATCHING_MODE_P2P) {
        result = setHelloOpt(helloOptlen, helloOpt);
        if (result != SCE_NET_ADHOC_MATCHING_OK) {
            getCalloutSyncing().closeCalloutThread();
            closeInputThread(emuenv, thread_id);
            closeEventHandler();
            closeSendSocket(emuenv, thread_id);
            return result;
        }

        addHelloTimedFunct(emuenv, helloInterval);
    }

    createMembersList();

    status = SCE_NET_ADHOC_MATCHING_CONTEXT_STATUS_RUNNING;
    return SCE_NET_ADHOC_MATCHING_OK;
}

int SceNetAdhocMatchingContext::stop(EmuEnvState &emuenv, SceUID thread_id) {
    status = SCE_NET_ADHOC_MATCHING_CONTEXT_STATUS_STOPPING;

    // These 3 may take time because they wait for both threads to end
    calloutSyncing.closeCalloutThread();
    closeInputThread(emuenv, thread_id);
    closeEventHandler();

    if (mode == SCE_NET_ADHOC_MATCHING_MODE_PARENT || mode == SCE_NET_ADHOC_MATCHING_MODE_P2P) {
        deleteHelloTimedFunction(emuenv);
        deleteHelloMessage();
    }

    deleteAllTargets(emuenv, thread_id);
    deleteMemberList();
    closeSendSocket(emuenv, thread_id);

    status = SCE_NET_ADHOC_MATCHING_CONTEXT_STATUS_NOT_RUNNING;
    return SCE_NET_ADHOC_MATCHING_OK;
}

int SceNetAdhocMatchingContext::initializeInputThread(EmuEnvState &emuenv, SceUID thread_id, int threadPriority, int threadStackSize, int threadCpuAffinityMask) {
    ZoneScopedC(0xF6C2FF);

    int socket_uid = CALL_EXPORT(sceNetSocket, "SceNetAdhocMatchingRecv", AF_INET, SCE_NET_SOCK_DGRAM_P2P, SCE_NET_IPPROTO_IP);
    if (socket_uid < SCE_NET_ADHOC_MATCHING_OK)
        return socket_uid;

    this->recvSocket = socket_uid;

    const int flag = 1;
    int result = CALL_EXPORT(sceNetSetsockopt, this->recvSocket, SCE_NET_SOL_SOCKET, SCE_NET_SO_REUSEADDR, &flag, sizeof(flag));
    if (result < SCE_NET_ADHOC_MATCHING_OK) {
        CALL_EXPORT(sceNetSocketClose, this->recvSocket);
        return result;
    }

    /* const int timeout = 1000;
    result = CALL_EXPORT(sceNetSetsockopt, this->recvSocket, SCE_NET_SOL_SOCKET, SCE_NET_SO_RCVTIMEO, &timeout, sizeof(timeout));
    if (result < SCE_NET_ADHOC_MATCHING_OK) {
        CALL_EXPORT(sceNetSocketClose, this->recvSocket);
        return result;
    }*/

    SceNetSockaddrIn recv_addr = {
        .sin_len = sizeof(SceNetSockaddrIn),
        .sin_family = AF_INET,
        .sin_port = htons(SCE_NET_ADHOC_DEFAULT_PORT),
        .sin_addr = htonl(INADDR_ANY),
        .sin_vport = htons(port),
    };

    auto bindResult = CALL_EXPORT(sceNetBind, this->recvSocket, (SceNetSockaddr *)&recv_addr, sizeof(SceNetSockaddrIn));

    if (bindResult < 0) {
        CALL_EXPORT(sceNetSocketClose, this->recvSocket);
        return bindResult;
    }
    const ThreadStatePtr input_thread = emuenv.kernel.create_thread(emuenv.mem, "SceAdhocMatchingInputThread", Ptr<void>(0), SCE_KERNEL_HIGHEST_PRIORITY_USER, SCE_KERNEL_THREAD_CPU_AFFINITY_MASK_DEFAULT, SCE_KERNEL_STACK_SIZE_USER_DEFAULT, nullptr);
    this->input_thread_id = thread_id;
    this->inputThread = std::thread(adhocMatchingInputThread, std::ref(emuenv), this->input_thread_id, this->id);
    this->inputThread.detach();
    return SCE_NET_ADHOC_MATCHING_OK;
}

void SceNetAdhocMatchingContext::closeInputThread(EmuEnvState &emuenv, SceUID thread_id) {
    ZoneScopedC(0xF6C2FF);
    // TODO!: abort all recvSocket operations
    if (this->inputThread.joinable())
        this->inputThread.join();

    CALL_EXPORT(sceNetShutdown, this->recvSocket, 0);
    CALL_EXPORT(sceNetSocketClose, this->recvSocket);
    this->recvSocket = 0;
}

int SceNetAdhocMatchingContext::initializeSendSocket(EmuEnvState &emuenv, SceUID thread_id) {
    ZoneScopedC(0xF6C2FF);
    SceNetInAddr ownAddr;
    CALL_EXPORT(sceNetCtlAdhocGetInAddr, &ownAddr);
    this->ownAddress = ownAddr.s_addr;

    int socket_uid = CALL_EXPORT(sceNetSocket, "SceNetAdhocMatchingSend", AF_INET, SCE_NET_SOCK_DGRAM_P2P, SCE_NET_IPPROTO_IP);
    if (socket_uid < SCE_NET_ADHOC_MATCHING_OK)
        return socket_uid;

    this->sendSocket = socket_uid;

    int result = SCE_NET_ADHOC_MATCHING_OK;
    int portOffset = this->mode == SCE_NET_ADHOC_MATCHING_MODE_PARENT ? 1 : 2;
    do {
        SceNetSockaddrIn addr = {
            .sin_len = sizeof(SceNetSockaddrIn),
            .sin_family = AF_INET,
            .sin_port = htons(SCE_NET_ADHOC_DEFAULT_PORT),
            .sin_vport = htons(this->port + portOffset),
        };
        this->ownPort = htons(SCE_NET_ADHOC_DEFAULT_PORT) + htons(this->port + portOffset);
        int result = CALL_EXPORT(sceNetBind, this->sendSocket, (SceNetSockaddr *)&addr, sizeof(SceNetSockaddrIn));
        portOffset++;
    } while (result == SCE_NET_ERROR_EADDRINUSE && portOffset < 20);

    if (result < SCE_NET_ADHOC_MATCHING_OK) {
        CALL_EXPORT(sceNetShutdown, this->sendSocket, 0);
        CALL_EXPORT(sceNetSocketClose, this->sendSocket);
        return result;
    }

    const int flag = 1;
    result = CALL_EXPORT(sceNetSetsockopt, this->sendSocket, SCE_NET_SOL_SOCKET, SCE_NET_SO_BROADCAST, &flag, sizeof(flag));
    if (result < SCE_NET_ADHOC_MATCHING_OK) {
        CALL_EXPORT(sceNetShutdown, this->sendSocket, 0);
        CALL_EXPORT(sceNetSocketClose, this->sendSocket);
        return result;
    }

    return SCE_NET_ADHOC_MATCHING_OK;
}

int SceNetAdhocMatchingContext::initializeEventHandler(EmuEnvState &emuenv, SceUID thread_id, int threadPriority, int threadStackSize, int threadCpuAffinityMask) {
    ZoneScopedC(0xF6C2FF);

    auto pipesResult = _pipe(this->msgPipeUid, 0x1000, 0);
    if (pipesResult < SCE_NET_ADHOC_MATCHING_OK) {
        return pipesResult;
    }
    const ThreadStatePtr event_thread = emuenv.kernel.create_thread(emuenv.mem, "SceAdhocMatchingEventThread", Ptr<void>(0), SCE_KERNEL_HIGHEST_PRIORITY_USER, SCE_KERNEL_THREAD_CPU_AFFINITY_MASK_DEFAULT, SCE_KERNEL_STACK_SIZE_USER_DEFAULT, nullptr);
    this->event_thread_id = event_thread->id;
    this->eventThread = std::thread(adhocMatchingEventThread, std::ref(emuenv), this->event_thread_id, this->id);
    this->eventThread.detach();

    return SCE_NET_ADHOC_MATCHING_OK;
}

void SceNetAdhocMatchingContext::closeEventHandler() {
    ZoneScopedC(0xF6C2FF);
    SceNetAdhocMatchingPipeMessage abortMsg{
        .type = SCE_NET_ADHOC_MATCHING_EVENT_ABORT,
    };
    write(this->msgPipeUid[1], &abortMsg, sizeof(SceNetAdhocMatchingPipeMessage));

    close(this->msgPipeUid[0]);
    close(this->msgPipeUid[1]);

    if (this->eventThread.joinable())
        this->eventThread.join();
}

void SceNetAdhocMatchingContext::closeSendSocket(EmuEnvState &emuenv, SceUID thread_id) {
    ZoneScopedC(0xF6C2FF);
    // TODO: abort send socket operations
    CALL_EXPORT(sceNetShutdown, this->sendSocket, 0);
    CALL_EXPORT(sceNetSocketClose, this->sendSocket);
}

SceNetAdhocMatchingContext *SceNetAdhocMatchingContext::getNext() {
    return this->next;
}

void SceNetAdhocMatchingContext::setNext(SceNetAdhocMatchingContext *next_context) {
    this->next = next_context;
}

SceUID SceNetAdhocMatchingContext::getId() const {
    return this->id;
}

void SceNetAdhocMatchingContext::setId(SceUID id) {
    this->id = id;
}

SceUShort16 SceNetAdhocMatchingContext::getPort() const {
    return this->port;
}

SceNetAdhocMatchingContextStatus SceNetAdhocMatchingContext::getStatus() const {
    return this->status;
}

SceNetAdhocMatchingMode SceNetAdhocMatchingContext::getMode() const {
    return this->mode;
}

SceNetAdhocMatchingCalloutSyncing& SceNetAdhocMatchingContext::getCalloutSyncing() {
    return this->calloutSyncing;
}

int SceNetAdhocMatchingContext::getReadPipeUid() const {
    return this->msgPipeUid[0];
}

int SceNetAdhocMatchingContext::getWritePipeUid() const {
    return this->msgPipeUid[1];
}

void SceNetAdhocMatchingContext::processPacketFromTarget(EmuEnvState &emuenv, SceUID thread_id, SceNetAdhocMatchingTarget &target) {
    ZoneScopedC(0xF6C2FF);
    SceNetAdhocMatchingPacketType packetType;
    memcpy(&packetType, target.rawPacket + 1, sizeof(SceNetAdhocMatchingPacketType));
    int count = 0;

    SceNetAdhocMatchingMode mode = (SceNetAdhocMatchingMode)this->mode;
    if (mode == SCE_NET_ADHOC_MATCHING_MODE_PARENT) {
        // If we received any of these 2 packets and we are the parent, ignore them, we dont care
        if (packetType == SCE_NET_ADHOC_MATCHING_PACKET_TYPE_HELLO)
            return;
        if (packetType == SCE_NET_ADHOC_MATCHING_PACKET_TYPE_MEMBER_LIST)
            return;
    } else {
        if (mode == SCE_NET_ADHOC_MATCHING_MODE_CHILD) {
            // Child shouldn't Acknowedge any hello message
            if (packetType == SCE_NET_ADHOC_MATCHING_PACKET_TYPE_HELLO_ACK)
                return;
        } else {
            if (isTargetAddressHigher(target) && packetType == SCE_NET_ADHOC_MATCHING_PACKET_TYPE_MEMBER_LIST)
                return;
        }
        if (packetType == SCE_NET_ADHOC_MATCHING_PACKET_TYPE_MEMBER_LIST_ACK)
            return;
    }

    if ((packetType == SCE_NET_ADHOC_MATCHING_PACKET_TYPE_HELLO_ACK || packetType == SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK3) && target.rawPacketLength - target.packetLength > 15) {
        memcpy(&count, target.rawPacket + target.packetLength, sizeof(count));
        count = ntohl(count);
        if (count != target.unk_5c) {
            switch (target.status) {
            case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED:
                break;
            case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_2:
            case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES:
                setTargetStatus(target, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED);
                deleteAllTimedFunctions(emuenv, target);
                notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_CANCEL, &target.addr);
                break;
            case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED:
                setTargetStatus(target, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED);
                deleteAllTimedFunctions(emuenv, target);
                notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_LEAVE, &target.addr);
                break;
            }
        }
    }

    auto targetCount = this->countTargetsWithStatusOrBetter(SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES);
    switch (packetType) {
    case SCE_NET_ADHOC_MATCHING_PACKET_TYPE_HELLO:
        if (target.packetLength - 4 > 7) {
            LOG_CRITICAL("Received hello");
            if (target.status == SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED) {
                memcpy(&target.unk_0c, target.rawPacket + 4, sizeof(target.unk_0c));
                // target.unk_0c = ntohl(target.unk_0c);
                memcpy(&target.keepAliveInterval, target.rawPacket + 8, sizeof(target.keepAliveInterval));
                // target.keepAliveInterval = ntohl(target.keepAliveInterval);
                if (target.rawPacketLength - target.packetLength > 0xf) {
                    memcpy(&target.unk_50, target.rawPacket + target.packetLength, sizeof(target.unk_50));
                }
            }
            if (targetCount + 1 < maxnum) {
                if (target.packetLength - 0xc < 1) {
                    this->notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_HELLO, &target.addr);
                } else {
                    this->notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_HELLO, &target.addr, target.packetLength - 0xc, target.rawPacket + 0xc);
                }
            }
        }
        break;
    case SCE_NET_ADHOC_MATCHING_PACKET_TYPE_HELLO_ACK:
        switch (target.status) {
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED: {
            if (targetCount + 1 < this->maxnum) {
                setTargetStatus(target, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_2);
                target.unk_5c = count;
                sendOptDataToTarget(emuenv, thread_id, target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK9, 0, NULL);
                int data_size = target.packetLength - 4;
                if (data_size < 1) {
                    this->notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_REQUEST, &target.addr);
                } else {
                    this->notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_REQUEST, &target.addr, data_size, target.rawPacket + 0x4);
                }
            } else {
                sendOptDataToTarget(emuenv, thread_id, target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_CANCEL, 0, NULL);
            }
            break;
        }
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_2:
            if (targetCount + 1 < this->maxnum) {
                sendOptDataToTarget(emuenv, thread_id, target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK9, 0, NULL);
            } else {
                setTargetStatus(target, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED);
                deleteAllTimedFunctions(emuenv, target);
                sendOptDataToTarget(emuenv, thread_id, target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_CANCEL, 0, NULL);
                this->notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_CANCEL, &target.addr);
            }
            break;
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES:
            sendOptDataToTarget(emuenv, thread_id, target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK3, target.optLength, target.opt);
            addRegisterTargetTimeout(emuenv, target);
            break;
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES2: {
            setTargetStatus(target, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES);
            target.unk_5c = count;
            sendOptDataToTarget(emuenv, thread_id, target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK3, target.optLength, target.opt);
            addRegisterTargetTimeout(emuenv, target);
            int data_size = target.packetLength - 4;
            if (data_size < 1) {
                this->notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_ACCEPT, &target.addr);
            } else {
                this->notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_ACCEPT, &target.addr, data_size, target.rawPacket + 0x4);
            }
            break;
        }
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED:
            setTargetStatus(target, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED);
            deleteAllTimedFunctions(emuenv, target);
            sendOptDataToTarget(emuenv, thread_id, target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_CANCEL, 0, NULL);
            this->notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_ERROR, &target.addr);
        }
        break;
    case SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK3:
        switch (target.status) {
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED:
            sendOptDataToTarget(emuenv, thread_id, target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_CANCEL, target.optLength, target.opt);
            break;
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_2:
            setTargetStatus(target, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED);
            deleteAllTimedFunctions(emuenv, target);
            sendOptDataToTarget(emuenv, thread_id, target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_CANCEL, 0, nullptr);
            notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_ERROR, &target.addr);
            break;
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES:
            setTargetStatus(target, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED);
            sendOptDataToTarget(emuenv, thread_id, target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK4, 0, nullptr);
            addTargetTimeout(emuenv, target);
            target.targetTimeout.retryCount = retryCount;
            notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_ESTABLISHED, &target.addr);
            break;
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES2:
            setTargetStatus(target, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED);
            target.unk_5c = count;
            sendOptDataToTarget(emuenv, thread_id, target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK4, 0, nullptr);
            addTargetTimeout(emuenv, target);
            target.targetTimeout.retryCount = retryCount;
            if (target.packetLength - 4 < 1) {
                notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_ACCEPT, &target.addr);
            } else {
                notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_ACCEPT, &target.addr, target.packetLength - 4, target.rawPacket + 4);
            }
            notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_ESTABLISHED, &target.addr);
            break;
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED:
            sendOptDataToTarget(emuenv, thread_id, target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK4, 0, nullptr);
            break;
        }
        break;
    case SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK4:
        switch (target.status) {
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED:
            sendOptDataToTarget(emuenv, thread_id, target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_CANCEL, target.optLength, target.opt);
            break;
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_2:
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES:
            setTargetStatus(target, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED);
            deleteAllTimedFunctions(emuenv, target);
            sendOptDataToTarget(emuenv, thread_id, target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_CANCEL, 0, nullptr);
            notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_ERROR, &target.addr);
            break;
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES2:
            setTargetStatus(target, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED);
            addRegisterTargetTimeout(emuenv, target);
            target.targetTimeout.retryCount = retryCount;
            notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_ESTABLISHED, &target.addr);
            break;
        }
        break;
    case SCE_NET_ADHOC_MATCHING_PACKET_TYPE_CANCEL:
        switch (target.status) {
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED:
            break;
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_2:
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES:
            setTargetStatus(target, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED);
            deleteAllTimedFunctions(emuenv, target);
            if (target.packetLength - 4 < 1) {
                notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_CANCEL, &target.addr);
            } else {
                notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_CANCEL, &target.addr, target.packetLength - 4,target.rawPacket + 4);
            }
            break;
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES2:
            setTargetStatus(target, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED);
            deleteAllTimedFunctions(emuenv, target);
            if (target.packetLength - 4 < 1) {
                notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_DENY, &target.addr);
            } else {
                notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_DENY, &target.addr, target.packetLength - 4, target.rawPacket + 4);
            }
            break;
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED:
            setTargetStatus(target, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED);
            deleteAllTimedFunctions(emuenv, target);
            if (target.packetLength - 4 < 1) {
                notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_LEAVE, &target.addr);
            } else {
                notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_LEAVE, &target.addr, target.packetLength - 4, target.rawPacket + 4);
            }
            break;
        }
        break;
    case SCE_NET_ADHOC_MATCHING_PACKET_TYPE_MEMBER_LIST:
        switch (target.status) {
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED:
            sendOptDataToTarget(emuenv, thread_id, target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_CANCEL, target.optLength, target.opt);
            break;
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_2:
            setTargetStatus(target, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED);
            deleteAllTimedFunctions(emuenv, target);
            sendOptDataToTarget(emuenv, thread_id, target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_CANCEL, 0, nullptr);
            notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_ERROR, &target.addr);
            break;
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES:
            setTargetStatus(target, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED);
            sendOptDataToTarget(emuenv, thread_id, target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_MEMBER_LIST_ACK, 0, nullptr);
            addTargetTimeout(emuenv, target);
            target.targetTimeout.retryCount = retryCount;
            if (mode == SCE_NET_ADHOC_MATCHING_MODE_CHILD) {
                processMemberListPacket(target.rawPacket, target.packetLength);
            }
            notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_ESTABLISHED, &target.addr);
            break;
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES2:
            setTargetStatus(target, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED);
            deleteAllTimedFunctions(emuenv, target);
            sendOptDataToTarget(emuenv, thread_id, target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_CANCEL, 0, nullptr);
            notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_ERROR, &target.addr);
            break;
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED:
            sendOptDataToTarget(emuenv, thread_id, target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_MEMBER_LIST_ACK, 0, nullptr);
            target.targetTimeout.retryCount = retryCount;
            if (mode == SCE_NET_ADHOC_MATCHING_MODE_CHILD) {
                processMemberListPacket(target.rawPacket, target.packetLength);
            }
        }
        break;
    case SCE_NET_ADHOC_MATCHING_PACKET_TYPE_MEMBER_LIST_ACK:
        switch (target.status) {
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED:
            sendOptDataToTarget(emuenv, thread_id, target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_CANCEL, target.optLength, target.opt);
            break;
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_2:
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES:
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES2:
            setTargetStatus(target, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED);
            deleteAllTimedFunctions(emuenv, target);
            sendOptDataToTarget(emuenv, thread_id, target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_CANCEL, target.optLength, target.opt);
            notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_ERROR, &target.addr);
            break;
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED:
            target.targetTimeout.retryCount = retryCount;
        }
        break;
    case SCE_NET_ADHOC_MATCHING_PACKET_TYPE_BYE:
        switch (target.status) {
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED:
            break;
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_2:
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES:
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES2:
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED:
            setTargetStatus(target, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED);
            deleteAllTimedFunctions(emuenv, target);
        }
        notifyHandler(emuenv, id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_BYE, &target.addr);
        target.delete_target = true;
        break;
    case SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK9:
        switch (target.status) {
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED:
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_2:
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES:
            break;
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES2:
            target.retryCount = retryCount;
        }
    case SCE_NET_ADHOC_MATCHING_PACKET_TYPE_DATA:
    case SCE_NET_ADHOC_MATCHING_PACKET_TYPE_DATA_ACK: {
        int other;
        if (target.status == SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED) {
            memcpy(&count, target.rawPacket + 4, sizeof(int));
            memcpy(&other, target.rawPacket + 4, sizeof(int));
            if (count == target.unk_5c) {
                if (packetType == SCE_NET_ADHOC_MATCHING_PACKET_TYPE_DATA) {
                    if (target.unk_64 <= other) {
                        target.unk_64 = other + 1;
                        if (target.packetLength - 0xc < 1) {
                            notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_DATA, &target.addr);
                        } else {
                            notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_DATA, &target.addr, target.packetLength - 0xc, target.rawPacket + 0xc);
                        }
                    }
                    sendDataMessageToTarget(emuenv, thread_id, target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_DATA_ACK, 0, nullptr);
                } else if (target.sendDataStatus == 2 && other == target.sendDataCount) {
                    target.setSendDataStatus(SCE_NET_ADHOC_MATCHING_CONTEXT_SEND_DATA_STATUS_READY);
                    notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_DATA_ACK, &target.addr);
                }
            }
        }
        break;
    }
    }
}

void SceNetAdhocMatchingContext::abortSendData(EmuEnvState &emuenv, SceNetAdhocMatchingTarget &target) {
    deleteSendDataTimeout(emuenv, target);
    target.setSendDataStatus(SCE_NET_ADHOC_MATCHING_CONTEXT_SEND_DATA_STATUS_READY);
}

int SceNetAdhocMatchingContext::cancelTargetWithOpt(EmuEnvState &emuenv, SceUID thread_id, SceNetAdhocMatchingTarget &target, SceSize optLen, char *opt) {
    if (target.status == SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED) {
        return SCE_NET_ADHOC_MATCHING_OK;
    }

    deleteAllTimedFunctions(emuenv, target);
    sendOptDataToTarget(emuenv, thread_id, target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_CANCEL, optLen, opt);
    setTargetStatus(target, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED);

    if (target.optLength > 0) {
        delete target.opt;
        target.optLength = 0;
        target.opt = nullptr;
    }

    if (optLen > 0) {
        target.opt = new char[optLen];
        if (target.opt == nullptr)
            return SCE_NET_ADHOC_MATCHING_ERROR_NO_SPACE;
        memcpy(target.opt, opt, optLen);
        target.optLength = optLen;
    }

    return SCE_NET_ADHOC_MATCHING_OK;
}

int SceNetAdhocMatchingContext::selectTarget(EmuEnvState &emuenv, SceUID thread_id, SceNetAdhocMatchingTarget &target, SceSize optLen, char *opt) {
    auto membersCount = countTargetsWithStatusOrBetter(SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES);
    switch (target.status) {
    case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED:
        if (mode == SCE_NET_ADHOC_MATCHING_MODE_PARENT)
            return SCE_NET_ADHOC_MATCHING_ERROR_TARGET_NOT_READY;
        if (membersCount + 1 >= maxnum)
            return SCE_NET_ADHOC_MATCHING_ERROR_EXCEED_MAXNUM;
        if (target.optLength > 0) {
            delete target.opt;
            target.optLength = 0;
            target.opt = nullptr;
        }
        if (optLen > 0) {
            target.opt = new char[optLen];
            if (target.opt == nullptr)
                return SCE_NET_ADHOC_MATCHING_ERROR_NO_SPACE;
            memcpy(target.opt, opt, optLen);
            target.optLength = optLen;
        }
        target.targetCount++;
        if (target.targetCount == 0)
            target.targetCount = 1;

        sendOptDataToTarget(emuenv, thread_id, target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_HELLO_ACK, target.optLength, target.opt);
        addRegisterTargetTimeout(emuenv, target);
        setTargetStatus(target, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES2);
        target.retryCount = retryCount;
        break;
    case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_2:
        if (membersCount + 1 >= maxnum)
            return SCE_NET_ADHOC_MATCHING_ERROR_EXCEED_MAXNUM;
        if (target.optLength > 0) {
            delete target.opt;
            target.optLength = 0;
            target.opt = nullptr;
        }
        if (optLen > 0) {
            target.opt = new char[optLen];
            if (target.opt == nullptr)
                return SCE_NET_ADHOC_MATCHING_ERROR_NO_SPACE;
            memcpy(target.opt, opt, optLen);
            target.optLength = optLen;
        }
        target.targetCount++;
        if (target.targetCount == 0)
            target.targetCount = 1;

       sendOptDataToTarget(emuenv, thread_id, target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK3, target.optLength, target.opt);
        addRegisterTargetTimeout(emuenv, target);
        setTargetStatus(target, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES2);
        target.retryCount = retryCount;
        break;
    case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES:
    case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES2:
        return SCE_NET_ADHOC_MATCHING_ERROR_REQUEST_IN_PROGRESS;
    case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED:
        return SCE_NET_ADHOC_MATCHING_ERROR_ALREADY_ESTABLISHED;
    }

    return SCE_NET_ADHOC_MATCHING_OK;
}

int SceNetAdhocMatchingContext::sendData(EmuEnvState &emuenv, SceUID thread_id, SceNetAdhocMatchingTarget &target, SceSize dataLen, char *data) {
    if (target.sendData != nullptr) {
        delete target.sendData;
        target.sendData = nullptr;
    }
    target.sendData = new char[dataLen];

    if (target.sendData == nullptr)
        return SCE_NET_ADHOC_MATCHING_ERROR_NO_SPACE;

    memcpy(target.sendData, data, dataLen);
    target.sendDataCount++;
    sendDataMessageToTarget(emuenv, thread_id, target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_DATA, dataLen, target.sendData);
    addSendDataTimeout(emuenv, target);
    target.setSendDataStatus(SCE_NET_ADHOC_MATCHING_CONTEXT_SEND_DATA_STATUS_BUSY);
    return SCE_NET_ADHOC_MATCHING_OK;
}


void SceNetAdhocMatchingContext::handleEventMessage(EmuEnvState &emuenv, SceUID thread_id, SceNetAdhocMatchingTarget *target) {
    target->pipeMsg28.isSheduled = false;
    if (target == nullptr)
        return;

    processPacketFromTarget(emuenv, thread_id, *target);
    if (target->rawPacket != nullptr)
        delete target->rawPacket;
    target->rawPacket = nullptr;
    target->rawPacketLength = 0;
}

void SceNetAdhocMatchingContext::handleEventRegistrationTimeout(EmuEnvState &emuenv, SceUID thread_id, SceNetAdhocMatchingTarget *target) {
    target->targetTimeout.message.isSheduled = false;
    if (target == nullptr)
        return;

    if (target->status == SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES2) {
        if (target->retryCount> 0) {
            target->retryCount--;
        }
        if (target->unk_50 || target->retryCount > 0) {
            sendOptDataToTarget(emuenv, thread_id, *target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_HELLO_ACK, target->optLength, target->opt);
            addRegisterTargetTimeout(emuenv, *target);
        } else {
            setTargetStatus(*target, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED);
            sendOptDataToTarget(emuenv, thread_id, *target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_CANCEL);
            notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_TIMEOUT, &target->addr);
        }
    }
    if (target->status == SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES) {
        target->retryCount++;
        if (target->retryCount < 1) {
            setTargetStatus(*target, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED);
            sendOptDataToTarget(emuenv, thread_id, *target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_CANCEL);
            notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_TIMEOUT, &target->addr);
        } else {
            sendOptDataToTarget(emuenv, thread_id, *target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK3, target->optLength, target->opt);
            addRegisterTargetTimeout(emuenv, *target);
        }
    }
}

void SceNetAdhocMatchingContext::handleEventTargetTimeout(EmuEnvState &emuenv, SceUID thread_id, SceNetAdhocMatchingTarget *target) {
    target->targetTimeout.message.isSheduled = false;
    if (target == nullptr)
        return;

    if (target->status != SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED) {
        return;
    }

    if (mode == SCE_NET_ADHOC_MATCHING_MODE_PARENT || (mode == SCE_NET_ADHOC_MATCHING_MODE_P2P && isTargetAddressHigher(*target))) {
        sendMemberListToTarget(target);
    }

    if (target->targetTimeout.retryCount-- > 0) {
        addTargetTimeout(emuenv, *target);
        return;
    }

    setTargetStatus(*target, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED);
    sendOptDataToTarget(emuenv, thread_id, *target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_CANCEL, 0, nullptr);
    notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_TIMEOUT, &target->addr, 0, nullptr);
}

void SceNetAdhocMatchingContext::handleEventHelloTimeout(EmuEnvState &emuenv, SceUID thread_id) {
    helloPipeMsg.isSheduled = false;

    const int num = countTargetsWithStatusOrBetter(SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES);
    if (num + 1 < maxnum)
        broadcastHello(emuenv, thread_id);

    addHelloTimedFunct(emuenv, helloInterval);
}

void SceNetAdhocMatchingContext::handleEventDataTimeout(EmuEnvState &emuenv, SceUID thread_id, SceNetAdhocMatchingTarget *target) {
    target->sendDataTimeout.message.isSheduled = false;
    if (target == nullptr)
        return;

    if (target->sendDataStatus != SCE_NET_ADHOC_MATCHING_CONTEXT_SEND_DATA_STATUS_BUSY)
        return;

    if (target->sendDataTimeout.retryCount-- > 0)
        return;

    target->setSendDataStatus(SCE_NET_ADHOC_MATCHING_CONTEXT_SEND_DATA_STATUS_READY);
    notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_DATA_TIMEOUT, &target->addr);
}

void SceNetAdhocMatchingContext::handleIncommingPackage(SceNetInAddr *addr, SceSize packetLength, SceSize bufferLength) {
    auto target = findTargetByAddr(addr);

    // No target found try to create one
    if (target == nullptr) {
        SceNetAdhocMatchingPacketType type = *(SceNetAdhocMatchingPacketType *)(rxbuf + 1);
        if (type == SCE_NET_ADHOC_MATCHING_PACKET_TYPE_HELLO_ACK && (mode == SCE_NET_ADHOC_MATCHING_MODE_PARENT || mode == SCE_NET_ADHOC_MATCHING_MODE_P2P)) {
            target = newTarget(addr);
        }
        if (type == SCE_NET_ADHOC_MATCHING_PACKET_TYPE_HELLO && (mode == SCE_NET_ADHOC_MATCHING_MODE_CHILD || mode == SCE_NET_ADHOC_MATCHING_MODE_P2P)) {
            target = newTarget(addr);
        }
    }

    // No target available
    if (target == nullptr) {
        return;
    }

    if (!target->pipeMsg28.isSheduled) {
        auto rawPacket = new char[bufferLength];
        if (rawPacket == nullptr)
            return;

        // Copy the whole packet we received into the peer
        memcpy(rawPacket, this->rxbuf, bufferLength);
        target->rawPacketLength = bufferLength;
        target->rawPacket = rawPacket;
        target->packetLength = packetLength + 4;
        target->keepAliveInterval = keepAliveInterval;

        target->pipeMsg28.type = SCE_NET_ADHOC_MATCHING_EVENT_PACKET;
        target->pipeMsg28.target = target;
        target->pipeMsg28.isSheduled = true;
        write(getWritePipeUid(), &target->pipeMsg28, sizeof(target->pipeMsg28));
    }
}

void SceNetAdhocMatchingTarget::setSendDataStatus(SceNetAdhocMatchingSendDataStatus status) {
    if (this->sendDataStatus == status)
        return;

    if (this->sendDataStatus == SCE_NET_ADHOC_MATCHING_CONTEXT_SEND_DATA_STATUS_BUSY && status == SCE_NET_ADHOC_MATCHING_CONTEXT_SEND_DATA_STATUS_READY) {
        delete this->sendData;
        this->sendData = nullptr;
    }
    this->sendDataStatus = status;
}

void SceNetAdhocMatchingTarget::setStatus(SceNetAdhocMatchingTargetStatus status) {
    if (this->status == status) {
        return;
    }

    bool is_target_in_progress = this->status == SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES || this->status == SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES2;
    bool is_status_not_in_progress = status != SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES2 && status != SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES2;

    if (is_target_in_progress && is_status_not_in_progress && this->packetLength > 0) {
        delete this->opt;
        this->packetLength = 0;
        this->opt = nullptr;
    }

    if (this->status != SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED && status == SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED) {
        this->sendDataCount = 0;
        this->unk_64 = 0;
    }

    if (this->status == SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED && status != SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED && this->sendDataStatus != SCE_NET_ADHOC_MATCHING_CONTEXT_SEND_DATA_STATUS_READY) {
        if (this->sendDataStatus == SCE_NET_ADHOC_MATCHING_CONTEXT_SEND_DATA_STATUS_BUSY) {
            delete this->sendData;
            this->sendData = nullptr;
        }
        this->sendDataStatus = SCE_NET_ADHOC_MATCHING_CONTEXT_SEND_DATA_STATUS_READY;
    }

    this->status = status;
}

void SceNetAdhocMatchingTarget::deleteRawPacket() {

}
void SceNetAdhocMatchingTarget::deleteOptMessage() {

}
void SceNetAdhocMatchingContext::setTargetStatus(SceNetAdhocMatchingTarget& target, SceNetAdhocMatchingTargetStatus status) {
    target.setStatus(status);
    if (target.status == SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED || status == SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED) {
        createMembersList();
    }
}

SceNetAdhocMatchingTarget *SceNetAdhocMatchingContext::newTarget(SceNetInAddr *addr) {
    ZoneScopedC(0xF6C2FF);
    auto *target = new SceNetAdhocMatchingTarget();

    if (target == nullptr) {
        return nullptr;
    }

    setTargetStatus(*target, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED);
    target->addr.s_addr = addr->s_addr;
    target->msgPipeUid[0] = this->msgPipeUid[0];
    target->msgPipeUid[1] = this->msgPipeUid[1];

    // SceNetInternal_689B9D7D(&target->targetCount); <-- GetCurrentTargetCount
    if (target->targetCount == 0) {
        target->targetCount = 1;
    }

    target->targetTimeout.isAckPending = false;
    if (target->sendDataStatus != SCE_NET_ADHOC_MATCHING_CONTEXT_SEND_DATA_STATUS_READY) {
        if (target->sendDataStatus == SCE_NET_ADHOC_MATCHING_CONTEXT_SEND_DATA_STATUS_BUSY && target->packetLength > 0) {
            delete target->rawPacket;
            target->packetLength = 0;
            target->rawPacket = nullptr;
        }
        target->sendDataStatus = SCE_NET_ADHOC_MATCHING_CONTEXT_SEND_DATA_STATUS_READY;
    }

    target->sendDataTimeout.isAckPending = false;
    target->next = targetList;
    targetList = target;

    return target;
}

SceNetAdhocMatchingTarget *SceNetAdhocMatchingContext::findTargetByAddr(SceNetInAddr* addr) const{
    ZoneScopedC(0xF6C2FF);
    SceNetAdhocMatchingTarget *target = this->targetList;
    while (target != nullptr) {
        if (target->addr.s_addr == addr->s_addr && !target->delete_target)
            return target;
        target = target->next;
    }

    return nullptr;
};

void SceNetAdhocMatchingContext::getMemberList(SceNetAdhocMatchingTargetStatus status, SceNetInAddr *addrList, SceSize &addrListSize) const {
    ZoneScopedC(0xF6C2FF);
    auto *target = targetList;
    SceSize index = 0;

    for (; target != nullptr; target = target->next) {
        if (target->status < status) {
            continue;
        }
        if (addrListSize <= index) {
            break;
        }
        if (addrList != nullptr) {
            memcpy(&addrList[index], &target->addr, sizeof(SceNetInAddr));
        }
        index++;
    }

    addrListSize = index;
}

SceSize SceNetAdhocMatchingContext::countTargetsWithStatusOrBetter(SceNetAdhocMatchingTargetStatus status) const {
    ZoneScopedC(0xF6C2FF);
    SceSize i = 0;
    SceNetAdhocMatchingTarget *target;
    for (target = this->targetList; target != nullptr; target = target->next) {
        if (target->status >= status)
            i++;
    }
    return i;
}

bool SceNetAdhocMatchingContext::isTargetAddressHigher(SceNetAdhocMatchingTarget &target) const {
    return ownAddress < target.addr.s_addr;
}

void SceNetAdhocMatchingContext::deleteTarget(SceNetAdhocMatchingTarget *target) {
    ZoneScopedC(0xF6C2FF);
    SceNetAdhocMatchingTarget *previous = nullptr;
    auto *current = this->targetList;

    // Find and remove target from list
    while (true) {
        if (current == nullptr) {
            return;
        }

        if (current == target) {
            if (previous == nullptr) {
                this->targetList = current->next;
                break;
            }
            previous->next = current->next;
            break;
        }

        previous = current;
        current = current->next;
    }

    if (target->optLength > 0)
        delete target->opt;
    if (target->sendData != nullptr)
        delete target->sendData;
    if (target->rawPacket != nullptr)
        delete target->rawPacket;

    delete target;
}

void SceNetAdhocMatchingContext::deleteAllTargets(EmuEnvState &emuenv, SceUID thread_id) {
    ZoneScopedC(0xF6C2FF);
    auto *target = this->targetList;
    while (target != nullptr) {
        deleteAllTimedFunctions(emuenv, *target);
        if (target->optLength > 0)
            delete target->opt;
        if (target->sendData != nullptr)
            delete target->sendData;
        if (target->rawPacket != nullptr)
            delete target->rawPacket;
        auto *nextTarget = target->next;
        delete target;
        target = nextTarget;
    }
    this->targetList = nullptr;
    broadcastBye(emuenv, thread_id);
}

int SceNetAdhocMatchingContext::createMembersList() {
    ZoneScopedC(0xF6C2FF);
    SceSize target_count = countTargetsWithStatusOrBetter(SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED);

    SceNetAdhocMatchingMemberMessage *message = new SceNetAdhocMatchingMemberMessage();

    if (message == nullptr)
        return SCE_NET_ADHOC_MATCHING_ERROR_NO_SPACE;

    message->header = {
        .one = 1,
        .type = SCE_NET_ADHOC_MATCHING_PACKET_TYPE_MEMBER_LIST,
    };
    message->parent.s_addr = this->ownAddress;
    message->members.resize(target_count);
    getMemberList(SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED, message->members.data(), target_count);
    message->header.packetLength = htons(target_count + 1);

    deleteMemberList();
    this->memberMsg = message;
    return SCE_NET_ADHOC_MATCHING_OK;
}

int SceNetAdhocMatchingContext::getMembers(SceSize &outMembersNum, SceNetAdhocMatchingMember *outMembers) const {
    if (this->memberMsg == nullptr) {
        outMembersNum = 0;
        return SCE_NET_ADHOC_MATCHING_OK;
    }

    if (outMembersNum > 0) {
        memcpy(&outMembers[0], &this->memberMsg->parent, sizeof(SceNetAdhocMatchingMember));
    }

    SceSize count = 1;
    for (SceSize i = 0; i < this->memberMsg->members.size(); i++) {
        if (count >= outMembersNum) {
            break;
        }
        if (outMembers != nullptr) {
            memcpy(&outMembers[count], &this->memberMsg->members[i], sizeof(SceNetAdhocMatchingMember));
        }
        count++;
    }

    outMembersNum = count;
    return SCE_NET_ADHOC_MATCHING_OK;
}

int SceNetAdhocMatchingContext::sendMemberListToTarget(SceNetAdhocMatchingTarget *target) {
    ZoneScopedC(0xF6C2FF);
    const int flags = 0x400; // 0x480 if sdk version < 0x1500000

    SceNetSockaddrIn addr = {
        .sin_len = sizeof(SceNetSockaddrIn),
        .sin_family = AF_INET,
        .sin_port = htons(SCE_NET_ADHOC_DEFAULT_PORT),
        .sin_addr = target->addr,
        .sin_vport = htons(port),
    };

    auto result = sendto(sendSocket, memberMsg->serialize().data(), memberMsg->messageSize(), flags, (sockaddr *)&addr, sizeof(SceNetSockaddrIn));

    if (result == SCE_NET_ERROR_EAGAIN) {
        result = SCE_NET_ADHOC_MATCHING_OK;
    }

    return result;
}

int SceNetAdhocMatchingContext::processMemberListPacket(char *packet, SceSize packetLength) {
    ZoneScopedC(0xF6C2FF);
    SceNetAdhocMatchingMemberMessage *message = new SceNetAdhocMatchingMemberMessage();

    if (message == nullptr)
        return SCE_NET_ADHOC_MATCHING_ERROR_NO_SPACE;

    message->parse(packet, packetLength);

    deleteMemberList();
    memberMsg = message;

    return SCE_NET_ADHOC_MATCHING_OK;
}

void SceNetAdhocMatchingContext::deleteMemberList() {
    if (this->memberMsg == nullptr)
        return;

    delete this->memberMsg;
    this->memberMsg = nullptr;
}

int SceNetAdhocMatchingContext::getHelloOpt(SceSize &outOptLen, void *outOpt) const {
    if (this->helloMsg == nullptr) {
        outOptLen = 0;
        return SCE_NET_ADHOC_MATCHING_OK;
    }

    if (this->helloMsg->optBuffer.size() < outOptLen) {
        outOptLen = this->helloMsg->optBuffer.size();
    }

    if (outOpt != nullptr && 0 < outOptLen) {
        memcpy(outOpt, this->helloMsg, outOptLen);
    }

    return SCE_NET_ADHOC_MATCHING_OK;
};

int SceNetAdhocMatchingContext::setHelloOpt(SceSize optlen, void *opt) {
    ZoneScopedC(0xF6C2FF);
    auto *message = new SceNetAdhocMatchingHelloMessage();

    if (message == nullptr)
        return SCE_NET_ADHOC_MATCHING_ERROR_NO_SPACE;

    message->header = {
        .one = 1,
        .type = SCE_NET_ADHOC_MATCHING_PACKET_TYPE_HELLO,
        .packetLength = htons(optlen + 8),
    };
    message->helloInterval = helloInterval;
    message->rexmtInterval = keepAliveInterval;
    message->unk_6c = 1;
    memset(message->zero, 0, 0xc);

    if (optlen > 0) {
        message->optBuffer.resize(optlen);
        memcpy(message->optBuffer.data(), opt, optlen);
    }

    deleteHelloMessage();
    this->helloMsg = message;
    return SCE_NET_ADHOC_MATCHING_OK;
};

void SceNetAdhocMatchingContext::deleteHelloMessage() {
    helloPipeMsg.isSheduled = false;
    if (this->helloMsg == nullptr)
        return;

    delete this->helloMsg;
    this->helloMsg = nullptr;
}

void SceNetAdhocMatchingContext::addHelloTimedFunct(EmuEnvState &emuenv, uint64_t time_interval) {
    ZoneScopedC(0xF6C2FF);
    // std::lock_guard<std::mutex> guard(emuenv.adhoc.getMutex());
    if (shouldHelloReqBeProcessed) {
        calloutSyncing.deleteTimedFunction(&helloTimedFunction);
        shouldHelloReqBeProcessed = false;
    }
    calloutSyncing.addTimedFunction(&helloTimedFunction, time_interval, &pipeHelloCallback, this);
    shouldHelloReqBeProcessed = true;
}

void SceNetAdhocMatchingContext::addSendDataTimeout(EmuEnvState &emuenv, SceNetAdhocMatchingTarget &target) {
    ZoneScopedC(0xF6C2FF);
    std::lock_guard<std::mutex> guard(emuenv.adhoc.getMutex());
    if (target.sendDataTimeout.isAckPending) {
        calloutSyncing.deleteTimedFunction(&target.sendDataTimeoutFunction);
        target.sendDataTimeout.isAckPending = false;
    }
    calloutSyncing.addTimedFunction(&target.sendDataTimeoutFunction, rexmtInterval, &sendDataTimeoutCallback, &target);
    target.sendDataTimeout.isAckPending = true;
}

void SceNetAdhocMatchingContext::addRegisterTargetTimeout(EmuEnvState &emuenv, SceNetAdhocMatchingTarget &target) {
    ZoneScopedC(0xF6C2FF);
    std::lock_guard<std::mutex> guard(emuenv.adhoc.getMutex());
    if (target.targetTimeout.isAckPending) {
        calloutSyncing.deleteTimedFunction(&target.targetTimeoutFunction);
        target.targetTimeout.isAckPending = false;
    }
    calloutSyncing.addTimedFunction(&target.targetTimeoutFunction, rexmtInterval, &registerTargetTimeoutCallback, &target);
    target.targetTimeout.isAckPending = true;
}

void SceNetAdhocMatchingContext::addTargetTimeout(EmuEnvState &emuenv, SceNetAdhocMatchingTarget &target) {
    ZoneScopedC(0xF6C2FF);
    int interval = keepAliveInterval;
    if (mode == SCE_NET_ADHOC_MATCHING_MODE_CHILD) {
        interval = target.keepAliveInterval;
    }

    std::lock_guard<std::mutex> guard(emuenv.adhoc.getMutex());
    if (target.targetTimeout.isAckPending) {
        calloutSyncing.deleteTimedFunction(&target.targetTimeoutFunction);
        target.targetTimeout.isAckPending = false;
    }
    calloutSyncing.addTimedFunction(&target.targetTimeoutFunction, interval, &targetTimeoutCallback, &target);
    target.targetTimeout.isAckPending = true;
}

void SceNetAdhocMatchingContext::deleteHelloTimedFunction(EmuEnvState &emuenv) {
    ZoneScopedC(0xF6C2FF);
    std::lock_guard<std::mutex> guard(emuenv.adhoc.getMutex());
    if (!shouldHelloReqBeProcessed)
        return;

    calloutSyncing.deleteTimedFunction(&this->helloTimedFunction);
    shouldHelloReqBeProcessed = false;
}

void SceNetAdhocMatchingContext::deleteSendDataTimeout(EmuEnvState &emuenv, SceNetAdhocMatchingTarget &target) {
    ZoneScopedC(0xF6C2FF);
    std::lock_guard<std::mutex> guard(emuenv.adhoc.getMutex());
    if (target.sendDataTimeout.isAckPending) {
        calloutSyncing.deleteTimedFunction(&target.sendDataTimeoutFunction);
        target.sendDataTimeout.isAckPending = false;
    }
}

void SceNetAdhocMatchingContext::deleteAllTimedFunctions(EmuEnvState &emuenv, SceNetAdhocMatchingTarget &target) {
    ZoneScopedC(0xF6C2FF);
    std::lock_guard<std::mutex> guard(emuenv.adhoc.getMutex());
    if (target.sendDataTimeout.isAckPending) {
        calloutSyncing.deleteTimedFunction(&target.sendDataTimeoutFunction);
        target.sendDataTimeout.isAckPending = false;
    }
    if (target.targetTimeout.isAckPending) {
        calloutSyncing.deleteTimedFunction(&target.targetTimeoutFunction);
        target.targetTimeout.isAckPending = false;
    }
}

void SceNetAdhocMatchingContext::notifyHandler(EmuEnvState &emuenv, SceUID thread_id, SceNetAdhocMatchingHandlerEventType type, SceNetInAddr *peer, SceSize optLen, void *opt) {
    ZoneScopedC(0xF6C2FF);
    if (!this->handler) {
        return;
    }

    Address vPeer = alloc(emuenv.mem, sizeof(SceNetInAddr), "adhocHandlerPeer");
    Address vOpt = alloc(emuenv.mem, optLen + 1, "adhocHandlerOpt");
    if (peer) {
        memcpy(Ptr<char>(vPeer).get(emuenv.mem), peer, sizeof(SceNetInAddr));
    }
    if (opt) {
        memcpy(Ptr<char>(vOpt).get(emuenv.mem), opt, optLen);
    }

    LOG_CRITICAL("NotifyHandler {}", (int)type);
    const ThreadStatePtr thread = lock_and_find(thread_id, emuenv.kernel.threads, emuenv.kernel.mutex);

    thread->run_adhoc_callback(this->handler, this->id, (uint32_t)type, Ptr<char>(vPeer), optLen, Ptr<char>(vOpt));

    free(emuenv.mem, vPeer); // free peer
    free(emuenv.mem, vOpt); // free opt
}

int SceNetAdhocMatchingContext::sendDataMessageToTarget(EmuEnvState &emuenv, SceUID thread_id, SceNetAdhocMatchingTarget &target, SceNetAdhocMatchingPacketType type, int datalen, char *data) {
    ZoneScopedC(0xF6C2FF);
    LOG_CRITICAL("Send message {}", (int)type);
    const int flags = 0x400; // 0x480 if sdk version < 0x1500000

    auto *msg = new SceNetAdhocMatchingDataMessage();

    msg->header = {
        .one = 1,
        .type = type,
        .packetLength = htons(datalen + 8),
    };

    msg->targetCount = htonl(target.targetCount);

    if (type == SCE_NET_ADHOC_MATCHING_PACKET_TYPE_DATA) {
        msg->other = target.sendDataCount;
    } else {
        msg->other = target.unk_64 - 1;
    }

    memcpy(msg->dataBuffer.data(), data, datalen);

    SceNetSockaddrIn addr = {
        .sin_len = sizeof(SceNetSockaddrIn),
        .sin_family = AF_INET,
        .sin_port = htons(SCE_NET_ADHOC_DEFAULT_PORT),
        .sin_addr = target.addr,
        .sin_vport = htons(port),
    };

    auto result = CALL_EXPORT(sceNetSendto, this->sendSocket, msg->serialize().data(), msg->messageSize(), flags, (SceNetSockaddr *)&addr, sizeof(SceNetSockaddrIn));

    if (result == SCE_NET_ERROR_EAGAIN) {
        result = SCE_NET_ADHOC_MATCHING_OK;
    }

    return result;
}

int SceNetAdhocMatchingContext::sendOptDataToTarget(EmuEnvState &emuenv, SceUID thread_id, SceNetAdhocMatchingTarget &target, SceNetAdhocMatchingPacketType type, int optlen, char *opt) {
    ZoneScopedC(0xF6C2FF);
    LOG_CRITICAL("Send OPT DATA {}", (int)type);
    const int flags = 0x400; // 0x480 if sdk version < 0x1500000
    int headerSize = 4;

    auto *msg = new SceNetAdhocMatchingOptMessage();

    if (type == SCE_NET_ADHOC_MATCHING_PACKET_TYPE_HELLO_ACK) {
        headerSize = 0x14;
    }
    if (type == SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK3) {
        headerSize = 0x14;
    }

    msg->header = {
        .one = 1,
        .type = type,
        .packetLength = htons(optlen),
    };

    if (optlen > 0) {
        msg->dataBuffer.resize(optlen);
        memcpy(msg->dataBuffer.data(), opt, optlen);
    }

    if (headerSize == 0x14) {
        msg->targetCount = target.targetCount;
        memset(msg->zero, 0, sizeof(msg->zero));
    }

    SceNetSockaddrIn addr = {
        .sin_len = sizeof(SceNetSockaddrIn),
        .sin_family = AF_INET,
        .sin_port = htons(SCE_NET_ADHOC_DEFAULT_PORT),
        .sin_addr = target.addr,
        .sin_vport = htons(port),
    };

    auto result = CALL_EXPORT(sceNetSendto, this->sendSocket, msg->serialize().data(), optlen + headerSize, flags, (SceNetSockaddr *)&addr, sizeof(SceNetSockaddrIn));

    if (result == SCE_NET_ERROR_EAGAIN) {
        result = SCE_NET_ADHOC_MATCHING_OK;
    }

    return result;
}

int SceNetAdhocMatchingContext::broadcastHello(EmuEnvState &emuenv, SceUID thread_id) {
    ZoneScopedC(0xF6C2FF);
    const int flags = 0x400; // 0x480 if sdk version < 0x1500000

    if (this->helloMsg == nullptr) {
        return SCE_NET_ADHOC_MATCHING_ERROR_INVALID_ARG;
    }

    SceNetSockaddrIn addr = {
        .sin_len = sizeof(SceNetSockaddrIn),
        .sin_family = AF_INET,
        .sin_port = htons(SCE_NET_ADHOC_DEFAULT_PORT),
        .sin_addr = htonl(INADDR_BROADCAST),
        .sin_vport = htons(this->port),
    };

    auto result = CALL_EXPORT(sceNetSendto, this->sendSocket, this->helloMsg->serialize().data(), this->helloMsg->messageSize(), flags, (SceNetSockaddr *)&addr, sizeof(SceNetSockaddrIn));
    if (result == SCE_NET_ERROR_EAGAIN)
        result = SCE_NET_ADHOC_MATCHING_OK;

    return result;
};

int SceNetAdhocMatchingContext::broadcastBye(EmuEnvState &emuenv, SceUID thread_id) {
    ZoneScopedC(0xF6C2FF);
    const int flags = 0x400; // 0x480 if sdk version < 0x1500000

    const SceNetAdhocMatchingMessageHeader byeMsg = {
        .one = 1,
        .type = SCE_NET_ADHOC_MATCHING_PACKET_TYPE_BYE,
        .packetLength = 0,
    };

    const SceNetSockaddrIn addr = {
        .sin_len = sizeof(SceNetSockaddrIn),
        .sin_family = AF_INET,
        .sin_port = htons(SCE_NET_ADHOC_DEFAULT_PORT),
        .sin_addr = INADDR_BROADCAST,
        .sin_vport = htons(this->port),
    };

    auto result = CALL_EXPORT(sceNetSendto, this->sendSocket, &byeMsg, sizeof(SceNetAdhocMatchingMessageHeader), flags, (SceNetSockaddr *)&addr, sizeof(SceNetSockaddrIn));
    if (result == SCE_NET_ERROR_EAGAIN)
        result = SCE_NET_ADHOC_MATCHING_OK;

    return result;
}

int registerTargetTimeoutCallback(void *args) {
    SceNetAdhocMatchingTarget *target = (SceNetAdhocMatchingTarget *)args;

    if (!target->targetTimeout.message.isSheduled) {
        target->targetTimeout.message = {
            .type = SCE_NET_ADHOC_MATCHING_EVENT_REGISTRATION_TIMEOUT,
            .target = target,
            .isSheduled = true,
        };
        write(target->msgPipeUid[1], &target->targetTimeout.message, sizeof(SceNetAdhocMatchingPipeMessage));
    }
    target->targetTimeout.isAckPending = false;
    return SCE_NET_ADHOC_MATCHING_OK;
}

int targetTimeoutCallback(void *args) {
    SceNetAdhocMatchingTarget *target = (SceNetAdhocMatchingTarget *)args;

    if (!target->targetTimeout.message.isSheduled) {
        target->targetTimeout.message = {
            .type = SCE_NET_ADHOC_MATCHING_EVENT_TARGET_TIMEOUT,
            .target = target,
            .isSheduled = true,
        };
        write(target->msgPipeUid[1], &target->targetTimeout.message, sizeof(SceNetAdhocMatchingPipeMessage));
    }
    target->targetTimeout.isAckPending = false;
    return SCE_NET_ADHOC_MATCHING_OK;
}

int sendDataTimeoutCallback(void *args) {
    ZoneScopedC(0xF6C2FF);
    SceNetAdhocMatchingTarget *target = (SceNetAdhocMatchingTarget *)args;

    if (!target->sendDataTimeout.message.isSheduled) {
        target->sendDataTimeout.message = {
            .type = SCE_NET_ADHOC_MATCHING_EVENT_DATA_TIMEOUT,
            .target = target,
            .isSheduled = true,
        };
        write(target->msgPipeUid[1], &target->sendDataTimeout.message, sizeof(SceNetAdhocMatchingPipeMessage));
    }
    target->sendDataTimeout.isAckPending = false;
    return SCE_NET_ADHOC_MATCHING_OK;
}

int pipeHelloCallback(void *args) {
    ZoneScopedC(0xF6C2FF);
    SceNetAdhocMatchingContext *ctx = (SceNetAdhocMatchingContext *)args;

    if (!ctx->helloPipeMsg.isSheduled) {
        ctx->helloPipeMsg.target = 0;
        ctx->helloPipeMsg.isSheduled = true;
        ctx->helloPipeMsg.type = SCE_NET_ADHOC_MATCHING_EVENT_HELLO_TIMEOUT;
        write(ctx->getWritePipeUid(), &ctx->helloPipeMsg, sizeof(SceNetAdhocMatchingPipeMessage));
    }
    ctx->shouldHelloReqBeProcessed = false;
    return SCE_NET_ADHOC_MATCHING_OK;
}