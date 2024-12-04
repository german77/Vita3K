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

    this->inputThread = std::thread(adhocMatchingInputThread, &emuenv, thread_id, this->id);
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
    int portOffset = 1;
    do {
        SceNetSockaddrIn addr = {
            .sin_len = sizeof(SceNetSockaddrIn),
            .sin_family = AF_INET,
            .sin_port = htons(SCE_NET_ADHOC_DEFAULT_PORT),
            .sin_vport = htons(this->port + portOffset),
        };
        this->ownPort = htons(this->port + portOffset);
        int result = CALL_EXPORT(sceNetBind, this->sendSocket, (SceNetSockaddr *)&addr, sizeof(SceNetSockaddrIn));
        portOffset++;
    } while (result == SCE_NET_ERROR_EADDRINUSE);

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

    this->eventThread = std::thread(adhocMatchingEventThread, &emuenv, thread_id, this->id);

    return SCE_NET_ADHOC_MATCHING_OK;
}

void SceNetAdhocMatchingContext::closeEventHandler() {
    ZoneScopedC(0xF6C2FF);

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

void SceNetAdhocMatchingContext::processPacketFromTarget(EmuEnvState &emuenv, SceUID thread_id, SceNetAdhocMatchingTarget *target) {
    ZoneScopedC(0xF6C2FF);
    SceNetAdhocMatchingPacketType packetType;
    memcpy(&packetType, target->rawPacket + 1, sizeof(SceNetAdhocMatchingPacketType));
    int count = 0;

    SceNetAdhocMatchingMode mode = (SceNetAdhocMatchingMode)this->mode;
    if (mode == SCE_NET_ADHOC_MATCHING_MODE_PARENT) {
        // If we received any of these 2 packets and we are the parent, ignore them, we dont care
        if (packetType == SCE_NET_ADHOC_MATCHING_PACKET_TYPE_HELLO)
            return;
        if (packetType == SCE_NET_ADHOC_MATCHING_PACKET_TYPE_ADDRS)
            return;
    } else {
        if (mode == SCE_NET_ADHOC_MATCHING_MODE_CHILD) {
            // Child shouldn't Acknowedge any hello message
            if (packetType == SCE_NET_ADHOC_MATCHING_PACKET_TYPE_HELLO_ACK)
                return;
        } else {
            // TODO
        }
        if (packetType == SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK7)
            return;
    }
    
    if ((packetType == SCE_NET_ADHOC_MATCHING_PACKET_TYPE_HELLO_ACK || packetType == SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK3) && target->rawPacketLength - target->packetLength > 15) {
        memcpy(&count, target->rawPacket + target->packetLength, sizeof(count));
        count = ntohl(count);
        if (count != target->unk_5c) {
            switch (target->status) {
            case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED:
                break;
            case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_2:
            case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES:
                setTargetStatus(target, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED);
                deleteAllTimedFunctions(emuenv, target);
                notifyHandler(&emuenv, this->id, 5, &target->addr, 0, nullptr);
                break;
            case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED:
                setTargetStatus(target, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED);
                deleteAllTimedFunctions(emuenv, target);
                notifyHandler(&emuenv, this->id, 3, &target->addr, 0, nullptr);
                break;
            }
        }
    }

    auto targetCount = this->countTargetsWithStatusOrBetter(SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES);
    switch (packetType) {
    case SCE_NET_ADHOC_MATCHING_PACKET_TYPE_HELLO:
        if (target->packetLength - 4 > 7) {
            LOG_CRITICAL("Received hello");
            if (target->status == SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED) {
                memcpy(&target->unk_0c, target->rawPacket + 4, sizeof(target->unk_0c));
                //target->unk_0c = ntohl(target->unk_0c);
                memcpy(&target->keepAliveInterval, target->rawPacket + 8, sizeof(target->keepAliveInterval));
                //target->keepAliveInterval = ntohl(target->keepAliveInterval);
                if (target->rawPacketLength - target->packetLength > 0xf) {
                    memcpy(&target->unk_50, target->rawPacket + target->packetLength, sizeof(target->unk_50));
                }
            }
            if (targetCount + 1 < maxnum) {
                if (target->packetLength - 0xc < 1) {
                    this->notifyHandler(&emuenv,id, 1, &target->addr, 0, nullptr);
                } else {
                    this->notifyHandler(&emuenv, id, 1, &target->addr, target->packetLength - 0xc, target->rawPacket + 0xc);
                }
            }
        }
        break;
    case SCE_NET_ADHOC_MATCHING_PACKET_TYPE_HELLO_ACK:
        switch (target->status) {
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED:{
            if (targetCount + 1 < this->maxnum) {
                setTargetStatus(target, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_2);
                target->unk_5c = count;
                sendOptDataToTarget(emuenv, thread_id, target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK9, 0, NULL);
                int data_size = target->packetLength - 4;
                if (data_size < 1) {
                    this->notifyHandler(&emuenv, id, 2, &target->addr, 0, nullptr);
                } else {
                    this->notifyHandler(&emuenv, id, 2, &target->addr, data_size, target->rawPacket + 0x4);
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
                this->notifyHandler(&emuenv, id, 5, &target->addr, 0, nullptr);
            }
            break;
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES:
            sendOptDataToTarget(emuenv, thread_id, target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK3, target->optLength, target->opt);
            add88TimedFunct(emuenv, target);
            break;
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES2:{
            setTargetStatus(target, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES);
            target->unk_5c = count;
            sendOptDataToTarget(emuenv, thread_id, target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK3, target->optLength, target->opt);
            add88TimedFunct(emuenv, target);
            int data_size = target->packetLength - 4;
            if (data_size < 1) {
                this->notifyHandler(&emuenv, id, 6, &target->addr, 0, nullptr);
            } else {
                this->notifyHandler(&emuenv, id, 6, &target->addr, data_size, target->rawPacket + 0x4);
            }
            break;
        }
        case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED:
            setTargetStatus(target, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED);
            deleteAllTimedFunctions(emuenv, target);
            sendOptDataToTarget(emuenv, thread_id, target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_CANCEL, 0, NULL);
            this->notifyHandler(&emuenv, id, 9, &target->addr, 0, nullptr);
        }
        break;
    case SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK3:
    case SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK4:
    case SCE_NET_ADHOC_MATCHING_PACKET_TYPE_CANCEL:
    case SCE_NET_ADHOC_MATCHING_PACKET_TYPE_ADDRS:
    case SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK7:
    case SCE_NET_ADHOC_MATCHING_PACKET_TYPE_BYE:
    case SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK9:
    case SCE_NET_ADHOC_MATCHING_PACKET_TYPE_DATA:
    case SCE_NET_ADHOC_MATCHING_PACKET_TYPE_DATA_ACK: break;
    };

    // TODO
}

void SceNetAdhocMatchingContext::setTargetSendDataStatus(SceNetAdhocMatchingTarget *target, int status) {
    ZoneScopedC(0xF6C2FF);
    if (target->sendDataStatus == status) {
        return;
    }
    if (target->sendDataStatus == 2 && status == 1 && target->sendDataLength > 0) {
        delete target->sendData;
        target->sendDataLength = 0;
        target->sendData = nullptr;
    }
    target->sendDataStatus = status;
}

void SceNetAdhocMatchingContext::setTargetStatus(SceNetAdhocMatchingTarget *target, SceNetAdhocMatchingTargetStatus status) {
    ZoneScopedC(0xF6C2FF);
    if (target->status == status) {
        return;
    }

    bool is_target_in_progress = target->status == SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES || target->status == SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES2;
    bool is_status_not_in_progress = status != SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES2 && status != SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES2;

    if (is_target_in_progress && is_status_not_in_progress && target->packetLength > 0) {
        delete target->opt;
        target->packetLength = 0;
        target->opt = nullptr;
    }

    if (target->status != SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED && status == SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED) {
        target->sendDataCount = 0;
        target->unk_64 = 0;
    }

    if (target->status == SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED && status != SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED && target->sendDataStatus != 1) {
        if (target->sendDataStatus == 2 && target->sendDataLength > 0) {
            delete target->sendData;
            target->sendDataLength = 0;
            target->sendData = nullptr;
        }
        target->sendDataStatus = 1;
    }

    if (target->status == SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED || status == SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED) {
        createMembersList();
    }

    target->status = status;
}

SceNetAdhocMatchingTarget *SceNetAdhocMatchingContext::newTarget(uint32_t addr) {
    ZoneScopedC(0xF6C2FF);
    auto *target = new SceNetAdhocMatchingTarget();

    if (target == nullptr) {
        return nullptr;
    }

    setTargetStatus(target, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED);
    target->addr.s_addr = addr;
    target->msgPipeUid[0] = this->msgPipeUid[0];
    target->msgPipeUid[1] = this->msgPipeUid[1];

    // SceNetInternal_689B9D7D(&target->targetCount); <-- GetCurrentTargetCount
    if (target->targetCount == 0) {
        target->targetCount = 1;
    }

    target->is_88_pending = false;
    if (target->sendDataStatus != 1) {
        if (target->sendDataStatus == 2 && target->packetLength > 0) {
            delete target->rawPacket;
            target->packetLength = 0;
            target->rawPacket = nullptr;
        }
        target->sendDataStatus = 1;
    }

    target->is_a0_pending = false;
    target->next = targetList;
    targetList = target;

    return target;
}

SceNetAdhocMatchingTarget *SceNetAdhocMatchingContext::findTargetByAddr(uint32_t addr) {
    ZoneScopedC(0xF6C2FF);
    SceNetAdhocMatchingTarget *target = this->targetList;
    while (target != nullptr) {
        if (target->addr.s_addr == addr && !target->unk_54)
            return target;
        target = target->next;
    }

    return nullptr;
};

void SceNetAdhocMatchingContext::getTargetAddrList(SceNetAdhocMatchingTargetStatus status, SceNetInAddr *addrList, SceSize &addrListSize) {
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

SceSize SceNetAdhocMatchingContext::countTargetsWithStatusOrBetter(SceNetAdhocMatchingTargetStatus status) {
    ZoneScopedC(0xF6C2FF);
    SceSize i = 0;
    SceNetAdhocMatchingTarget *target;
    for (target = this->targetList; target != nullptr; target = target->next) {
        if (target->status >= status)
            i++;
    }
    return i;
}

bool SceNetAdhocMatchingContext::isTargetAddressHigher(SceNetAdhocMatchingTarget *target) {
    ZoneScopedC(0xF6C2FF);
    return ownAddress < target->addr.s_addr;
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
    if (target->sendDataLength > 0)
        delete target->sendData;
    if (target->rawPacket != nullptr)
        delete target->rawPacket;

    delete target;
}

void SceNetAdhocMatchingContext::deleteAllTargets(EmuEnvState &emuenv, SceUID thread_id) {
    ZoneScopedC(0xF6C2FF);
    auto *target = this->targetList;
    while (target != nullptr) {
        deleteAllTimedFunctions(emuenv, target);
        if (target->optLength > 0)
            delete target->opt;
        if (target->sendDataLength > 0)
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
    SceSize length = target_count * sizeof(SceNetInAddr) + sizeof(SceNetInAddr) + 4;

    SceNetAdhocMatchingMemberMessage *message = new SceNetAdhocMatchingMemberMessage();

    if (message == nullptr)
        return SCE_NET_ADHOC_MATCHING_ERROR_NO_SPACE;

    message->one = 1;
    message->type = SCE_NET_ADHOC_MATCHING_PACKET_TYPE_ADDRS;
    message->packetLength = htons(length);
    message->parent.s_addr = ownAddress;
    message->members.resize(target_count);
    getTargetAddrList(SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED, message->members.data(), target_count);

    if (memberMsg != nullptr) {
        delete memberMsg;
    }

    memberMsgSize = length;
    memberMsg = message;
    return SCE_NET_ADHOC_MATCHING_OK;
}

int SceNetAdhocMatchingContext::getMembers(SceSize *membersNum, SceNetAdhocMatchingMember *members) {
    ZoneScopedC(0xF6C2FF);
    SceSize member_count = (memberMsgSize - 4) / sizeof(SceNetInAddr);
    SceSize count = 0;

    if (*membersNum > 0 && member_count > 0) {
        memcpy(&members[count], &memberMsg->parent, sizeof(SceNetAdhocMatchingMember));
        count++;
    }

    for (SceSize i = 0; i < member_count -1; i++) {
        if (count >= *membersNum) {
            break;
        }
        if (members != nullptr) {
            memcpy(&members[count], &memberMsg->members[i], sizeof(SceNetAdhocMatchingMember));
        }
        count++;
    }

    *membersNum = count;
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

    auto result = sendto(sendSocket, (char *)memberMsg, memberMsgSize, flags, (sockaddr *)&addr, sizeof(SceNetSockaddrIn));

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

    if (packetLength > sizeof(SceNetAdhocMatchingMemberMessage)) {
        return SCE_NET_ADHOC_MATCHING_ERROR_NO_SPACE;
    }

    message->members.resize((packetLength - 8) / sizeof(SceNetInAddr));
    memcpy(message, packet, packetLength);

    if (memberMsg != nullptr) {
        delete memberMsg;
    }

    memberMsgSize = packetLength;
    memberMsg = message;

    return SCE_NET_ADHOC_MATCHING_OK;
}

void SceNetAdhocMatchingContext::clearMemberList() {
    ZoneScopedC(0xF6C2FF);
    if (memberMsgSize == 0) {
        return;
    }
    delete memberMsg;
    memberMsg = nullptr;
    memberMsgSize = 0;
}

int SceNetAdhocMatchingContext::getHelloOpt(SceSize *oOptlen, void *oOpt) {
    ZoneScopedC(0xF6C2FF);
    int optLen = this->totalHelloLength - 0xc;

    if (oOpt != nullptr && 0 < optLen) {
        // Whichever is less to not write more than app allocated for opt data
        if (*oOptlen < optLen)
            optLen = *oOptlen;

        memcpy(oOpt, this->helloMsg, optLen);
    }

    *oOptlen = optLen;
    return SCE_NET_ADHOC_MATCHING_OK;
};

int SceNetAdhocMatchingContext::setHelloOpt(SceSize optlen, void *opt) {
    ZoneScopedC(0xF6C2FF);
    auto *message = new SceNetAdhocMatchingHelloMessage();

    if (message == nullptr)
        return SCE_NET_ADHOC_MATCHING_ERROR_NO_SPACE;

    message->one = 1;
    message->type = SCE_NET_ADHOC_MATCHING_PACKET_TYPE_HELLO;
    message->packetLength = htons(optlen + 8);
    message->helloInterval = helloInterval;
    message->rexmtInterval = keepAliveInterval;
    message->unk_6c = 1;
    memset(message->zero, 0, 0xc);

    if (optlen > 0) {
        message->optBuffer.resize(optlen);
        memcpy(message->optBuffer.data(), opt, optlen);
    }

    if (helloMsg != nullptr) {
        delete helloMsg;
    }

    totalHelloLength = optlen + 0x1c;
    helloMsg = message;
    return SCE_NET_ADHOC_MATCHING_OK;
};

int SceNetAdhocMatchingContext::broadcastHello(EmuEnvState &emuenv, SceUID thread_id) {
    ZoneScopedC(0xF6C2FF);
    const int flags = 0x400; // 0x480 if sdk version < 0x1500000

    SceNetSockaddrIn addr = {
        .sin_len = sizeof(SceNetSockaddrIn),
        .sin_family = AF_INET,
        .sin_port = htons(SCE_NET_ADHOC_DEFAULT_PORT),
        .sin_addr = htonl(INADDR_BROADCAST),
        .sin_vport = htons(port),
    };

    auto result = CALL_EXPORT(sceNetSendto, this->sendSocket, this->helloMsg->serialize().data(), this->totalHelloLength, flags, (SceNetSockaddr *)&addr, sizeof(SceNetSockaddrIn));

    if (result == SCE_NET_ERROR_EAGAIN)
        result = SCE_NET_ADHOC_MATCHING_OK;

    return result;
};

void SceNetAdhocMatchingContext::resetHelloOpt() {
    ZoneScopedC(0xF6C2FF);
    if (this->totalHelloLength == 0)
        return;

    delete this->helloMsg;
    this->helloMsg = nullptr;
    this->totalHelloLength = 0;
}

void SceNetAdhocMatchingContext::addHelloTimedFunct(EmuEnvState &emuenv, uint64_t time_interval) {
    ZoneScopedC(0xF6C2FF);
    //std::lock_guard<std::mutex> guard(emuenv.adhoc.getMutex());
    if (shouldHelloReqBeProcessed) {
        calloutSyncing.deleteTimedFunction(&helloTimedFunction, nullptr);
        shouldHelloReqBeProcessed = false;
    }
    calloutSyncing.addTimedFunction(&helloTimedFunction, time_interval, &pipeHelloCallback, this);
    shouldHelloReqBeProcessed = true;
}

void SceNetAdhocMatchingContext::addA0TimedFunction(EmuEnvState &emuenv, SceNetAdhocMatchingTarget *target) {
    ZoneScopedC(0xF6C2FF);
    std::lock_guard<std::mutex> guard(emuenv.adhoc.getMutex());
    if (target->is_a0_pending) {
        calloutSyncing.deleteTimedFunction(&target->timedFunctionA0, nullptr);
        target->is_a0_pending = false;
    }
    calloutSyncing.addTimedFunction(&target->timedFunctionA0, rexmtInterval, &pipeA0Callback, target);
    target->is_a0_pending = true;
}

void SceNetAdhocMatchingContext::add88TimedFunct(EmuEnvState &emuenv, SceNetAdhocMatchingTarget *target) {
    ZoneScopedC(0xF6C2FF);
    std::lock_guard<std::mutex> guard(emuenv.adhoc.getMutex());
    if (target->is_88_pending) {
        calloutSyncing.deleteTimedFunction(&target->timedFunction88, nullptr);
        target->is_88_pending = false;
    }
    calloutSyncing.addTimedFunction(&target->timedFunction88, rexmtInterval, &pipe88CallbackType2, target);
    target->is_88_pending = true;
}

void SceNetAdhocMatchingContext::add88TimedFunctionWithParentInterval(EmuEnvState &emuenv, SceNetAdhocMatchingTarget *target) {
    ZoneScopedC(0xF6C2FF);
    int interval = keepAliveInterval;
    if (mode == SCE_NET_ADHOC_MATCHING_MODE_CHILD) {
        interval = target->keepAliveInterval;
    }

    std::lock_guard<std::mutex> guard(emuenv.adhoc.getMutex());
    if (target->is_88_pending) {
        calloutSyncing.deleteTimedFunction(&target->timedFunction88, nullptr);
        target->is_88_pending = false;
    }
    calloutSyncing.addTimedFunction(&target->timedFunction88, interval, &pipe88CallbackType3, target);
    target->is_88_pending = true;
}

void SceNetAdhocMatchingContext::deleteHelloTimedFunction(EmuEnvState &emuenv) {
    ZoneScopedC(0xF6C2FF);
    std::lock_guard<std::mutex> guard(emuenv.adhoc.getMutex());
    if (!shouldHelloReqBeProcessed)
        return;

    calloutSyncing.deleteTimedFunction(&this->helloTimedFunction, nullptr);
    shouldHelloReqBeProcessed = false;
}

void SceNetAdhocMatchingContext::deleteA0TimedFunction(EmuEnvState &emuenv, SceNetAdhocMatchingTarget *target) {
    ZoneScopedC(0xF6C2FF);
    std::lock_guard<std::mutex> guard(emuenv.adhoc.getMutex());
    if (target->is_a0_pending) {
        calloutSyncing.deleteTimedFunction(&target->timedFunctionA0, nullptr);
        target->is_a0_pending = false;
    }
}

void SceNetAdhocMatchingContext::deleteAllTimedFunctions(EmuEnvState &emuenv, SceNetAdhocMatchingTarget *target) {
    ZoneScopedC(0xF6C2FF);
    std::lock_guard<std::mutex> guard(emuenv.adhoc.getMutex());
    if (target->is_a0_pending) {
        calloutSyncing.deleteTimedFunction(&target->timedFunctionA0, nullptr);
        target->is_a0_pending = false;
    }
    if (target->is_88_pending) {
        calloutSyncing.deleteTimedFunction(&target->timedFunction88, nullptr);
        target->is_88_pending = false;
    }
}

void SceNetAdhocMatchingContext::notifyHandler(EmuEnvState *emuenv, int context_id, int type, SceNetInAddr *peer, SceSize optLen, void *opt) {
    ZoneScopedC(0xF6C2FF);
    if (!this->handler.pc) {
        return;
    }

    const Address vPeer =alloc(emuenv->mem, sizeof(SceNetInAddr), "adhocHandlerPeer");
    const Address vOpt = alloc(emuenv->mem, optLen, "adhocHandlerOpt");
    if (peer) {
        memcpy(Ptr<char>(vPeer).get(emuenv->mem), peer, sizeof(SceNetInAddr));
    }
    if (opt) {
        memcpy(Ptr<char>(vOpt).get(emuenv->mem), opt, optLen);
    }

    SceNetAdhocHandlerArguments handleArgs{
        .id = this->id,
        .event = 1,
        .peer = vPeer,
        .optlen = optLen,
        .opt = vOpt
    };

    auto data = Ptr<SceNetAdhocHandlerArguments>(alloc(emuenv->mem, sizeof(handleArgs), "handleArgs"));
    memcpy(data.get(emuenv->mem), &handleArgs, sizeof(handleArgs));

    const ThreadStatePtr thread = lock_and_find(this->handler.thread, emuenv->kernel.threads, emuenv->kernel.mutex);
    thread->run_guest_function(this->handler.pc, sizeof(SceNetAdhocHandlerArguments), data);
    //thread->run_callback(handler.pc, { id, 1, vPeer, optLen, vOpt });

    free(emuenv->mem, vPeer); // free peer
    free(emuenv->mem, vOpt); // free opt
}

int SceNetAdhocMatchingContext::sendDataMessageToTarget(EmuEnvState &emuenv, SceUID thread_id, SceNetAdhocMatchingTarget *target, SceNetAdhocMatchingPacketType type, int datalen, char *data) {
    ZoneScopedC(0xF6C2FF);
    LOG_CRITICAL("Send message");
    const int flags = 0x400; // 0x480 if sdk version < 0x1500000

    auto *msg = new SceNetAdhocMatchingDataMessage();

    msg->one = 1;
    msg->type = type;
    msg->packetLength = htons(datalen + 8);
    msg->targetCount = htonl(target->targetCount);

    if (type == SCE_NET_ADHOC_MATCHING_PACKET_TYPE_DATA) {
        msg->other = target->sendDataCount;
    } else {
        msg->other = target->unk_64 - 1;
    }

    memcpy(msg->data, data, datalen);

    SceNetSockaddrIn addr = {
        .sin_len = sizeof(SceNetSockaddrIn),
        .sin_family = AF_INET,
        .sin_port = htons(SCE_NET_ADHOC_DEFAULT_PORT),
        .sin_addr = target->addr,
        .sin_vport = htons(port),
    };

    auto result = CALL_EXPORT(sceNetSendto, this->sendSocket, &msg, datalen + 0xc, flags, (SceNetSockaddr *)&addr, sizeof(SceNetSockaddrIn));

    if (result == SCE_NET_ERROR_EAGAIN) {
        result = SCE_NET_ADHOC_MATCHING_OK;
    }

    return result;
}

int SceNetAdhocMatchingContext::sendOptDataToTarget(EmuEnvState &emuenv, SceUID thread_id, SceNetAdhocMatchingTarget *target, SceNetAdhocMatchingPacketType type, int optlen, char *opt) {
    ZoneScopedC(0xF6C2FF);
    LOG_CRITICAL("Send OPT DATA");
    const int flags = 0x400; // 0x480 if sdk version < 0x1500000
    int headerSize = 4;

    auto *msg = new SceNetAdhocMatchingOptMessage();

    if (type == SCE_NET_ADHOC_MATCHING_PACKET_TYPE_HELLO_ACK) {
        headerSize = 0x14;
    }
    if (type == SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK3) {
        headerSize = 0x14;
    }

    msg->one = 1;
    msg->type = type;
    msg->packetLength = htons(optlen);

    if (optlen > 0) {
        msg->data.resize(optlen);
        memcpy(msg->data.data(), opt, optlen);
    }

    if (headerSize == 0x14) {
        msg->targetCount = target->targetCount;
        memset(msg->zero, 0, sizeof(msg->zero));
    }

    SceNetSockaddrIn addr = {
        .sin_len = sizeof(SceNetSockaddrIn),
        .sin_family = AF_INET,
        .sin_port = htons(SCE_NET_ADHOC_DEFAULT_PORT),
        .sin_addr = target->addr,
        .sin_vport = htons(port),
    };

    auto result = CALL_EXPORT(sceNetSendto, this->sendSocket, &msg, optlen + headerSize, flags, (SceNetSockaddr *)&addr, sizeof(SceNetSockaddrIn));

    if (result == SCE_NET_ERROR_EAGAIN) {
        result = SCE_NET_ADHOC_MATCHING_OK;
    }

    return result;
}

int SceNetAdhocMatchingContext::broadcastBye(EmuEnvState &emuenv, SceUID thread_id) {
    ZoneScopedC(0xF6C2FF);
    LOG_CRITICAL("BROADCAST BYE");
    const int flags = 0x400; // 0x480 if sdk version < 0x1500000

    const SceNetAdhocMatchingByeMessage byeMsg = {
        .one = 1,
        .type = SCE_NET_ADHOC_MATCHING_PACKET_TYPE_BYE,
        .packetLength = 0,
    };

    const SceNetSockaddrIn addr = {
        .sin_len = sizeof(SceNetSockaddrIn),
        .sin_family = AF_INET,
        .sin_port = htons(SCE_NET_ADHOC_DEFAULT_PORT),
        .sin_addr = INADDR_BROADCAST,
        .sin_vport = htons(port),
    };

    auto result = CALL_EXPORT(sceNetSendto, this->sendSocket, &byeMsg, sizeof(SceNetAdhocMatchingByeMessage), flags, (SceNetSockaddr *)&addr, sizeof(SceNetSockaddrIn));

    if (result == SCE_NET_ERROR_EAGAIN)
        result = SCE_NET_ADHOC_MATCHING_OK;

    return result;
}

int pipe88CallbackType2(void *args) {
    ZoneScopedC(0xF6C2FF);
    SceNetAdhocMatchingTarget *target = (SceNetAdhocMatchingTarget *)args;

    if ((target->pipeMsg88.flags & 1) == 0) {
        target->pipeMsg88.peer = target;
        target->pipeMsg88.flags |= 1;
        target->pipeMsg88.type = SCE_NET_ADHOC_MATCHING_EVENT_UNK2;
        write(target->msgPipeUid[1], &target->pipeMsg88, sizeof(SceNetAdhocMatchingPipeMessage));
    }
    target->is_88_pending = false;
    return SCE_NET_ADHOC_MATCHING_OK;
}

int pipe88CallbackType3(void *args) {
    ZoneScopedC(0xF6C2FF);
    SceNetAdhocMatchingTarget *target = (SceNetAdhocMatchingTarget *)args;

    if ((target->pipeMsg88.flags & 1) == 0) {
        target->pipeMsg88.peer = target;
        target->pipeMsg88.flags |= 1;
        target->pipeMsg88.type = SCE_NET_ADHOC_MATCHING_EVENT_UNK3;
        write(target->msgPipeUid[1], &target->pipeMsg88, sizeof(SceNetAdhocMatchingPipeMessage));
    }
    target->is_88_pending = false;
    return SCE_NET_ADHOC_MATCHING_OK;
}

int pipeA0Callback(void *args) {
    ZoneScopedC(0xF6C2FF);
    SceNetAdhocMatchingTarget *target = (SceNetAdhocMatchingTarget *)args;

    if ((target->msgA0.flags & 1) == 0) {
        target->msgA0.peer = target;
        target->msgA0.flags |= 1;
        target->msgA0.type = SCE_NET_ADHOC_MATCHING_EVENT_UNK5;
        write(target->msgPipeUid[1], &target->msgA0, sizeof(SceNetAdhocMatchingPipeMessage));
    }
    target->is_a0_pending = false;
    return SCE_NET_ADHOC_MATCHING_OK;
}

int pipeHelloCallback(void *args) {
    ZoneScopedC(0xF6C2FF);
    SceNetAdhocMatchingContext *ctx = (SceNetAdhocMatchingContext *)args;

    if ((ctx->helloPipeMsg.flags & 1) == 0) {
        ctx->helloPipeMsg.peer = 0;
        ctx->helloPipeMsg.flags |= 1;
        ctx->helloPipeMsg.type = SCE_NET_ADHOC_MATCHING_EVENT_HELLO_SEND;
        write(ctx->msgPipeUid[1], &ctx->helloPipeMsg, sizeof(SceNetAdhocMatchingPipeMessage));
    }
    ctx->shouldHelloReqBeProcessed = false;
    return SCE_NET_ADHOC_MATCHING_OK;
}