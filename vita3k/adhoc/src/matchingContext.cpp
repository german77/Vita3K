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
#include <util/types.h>

#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <Ws2tcpip.h>
#include <iphlpapi.h>
#include <winsock2.h>
#undef s_addr
typedef SOCKET abs_socket;
typedef int socklen_t;
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int abs_socket;
#endif

bool SceNetAdhocMatchingContext::initializeSendSocket(EmuEnvState &emuenv, SceUID thread_id) {
    SceNetInAddr ownAddr;
    CALL_EXPORT(sceNetCtlAdhocGetInAddr, &ownAddr);
    this->ownAddress = ownAddr.s_addr;

    this->sendSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (this->sendSocket < 0)
        return false; // what to return here

    sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(this->port + 1)
    };

    auto bindResult = bind(this->sendSocket, (sockaddr *)&addr, sizeof(addr));
    if (bindResult < 0) {
        shutdown(this->sendSocket, 0);
        close(this->sendSocket);
        return false;
    }

    char flag = 1;
    if (setsockopt(this->sendSocket, SOL_SOCKET, SO_BROADCAST, &flag, sizeof(flag)) < 0)
        return false; // what to return here

    return true;
}

bool SceNetAdhocMatchingContext::initEventHandler(EmuEnvState &emuenv) {
    /* auto pipesResult = pipe(this->pipesFd);
    if (pipesResult == -1) {
        assert(false);
        return false;
    }*/

    this->eventThread = std::thread(adhocMatchingEventThread, &emuenv, this->id);
    return true;
}

bool SceNetAdhocMatchingContext::initInputThread(EmuEnvState &emuenv) {
    this->recvSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (this->recvSocket < 0)
        return false;

    sockaddr_in recv_addr = {
        .sin_family = AF_INET,
        .sin_port = this->port
    };

#ifdef _WIN32
    recv_addr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
#else
    recv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
#endif

    auto bindResult = bind(this->recvSocket, (sockaddr *)&recv_addr, sizeof(recv_addr));

    if (bindResult < 0)
        close(this->recvSocket);

    this->inputThread = std::thread(adhocMatchingInputThread, &emuenv, this->id);
    return true;
}

bool SceNetAdhocMatchingContext::initCalloutThread(EmuEnvState &emuenv) {
    if (this->calloutSyncing.calloutThreadIsRunning)
        return false;

    this->calloutSyncing.calloutShouldExit = false;

    this->calloutSyncing.calloutThread = std::thread(adhocMatchingCalloutThread, &emuenv, this->id);
    this->calloutSyncing.calloutThreadIsRunning = true;
    return true;
};

void SceNetAdhocMatchingContext::unInitInputThread() {
    // TODO!: abort all recvSocket operations
    if (this->inputThread.joinable())
        this->inputThread.join();

#ifdef _WIN32
    shutdown(this->recvSocket, SD_BOTH);
#else
    shutdown(this->recvSocket, SHUT_RDWR);
#endif

    close(this->recvSocket);
    this->recvSocket = 0;
}

void SceNetAdhocMatchingContext::closeEventThread() {
    // TODO: abort all pipe operations and make read operation return negative value on the pipe

    if (this->eventThread.joinable())
        this->eventThread.join();

    // TODO: delete pipe here
}

void SceNetAdhocMatchingContext::closetSendSocket() {
    // TODO: abort send socket operations
    shutdown(this->sendSocket, 0);
    close(this->sendSocket);
}

void SceNetAdhocMatchingContext::closeAddrMsg() {
    if (this->addrMsgLen <= 0)
        return;

    this->addrMsgLen = 0;
    delete this->addrMsg;
    this->addrMsg = 0;
}

void SceNetAdhocMatchingContext::destroy(EmuEnvState &emuenv, SceUID thread_id, const char *export_name) {
    SceNetAdhocMatchingContext *prev = emuenv.adhoc.adhocMatchingContextsList; // empty header
    SceNetAdhocMatchingContext *current = emuenv.adhoc.adhocMatchingContextsList->next; // the first valid node

    // Context Pointer
    SceNetAdhocMatchingContext *item = emuenv.adhoc.adhocMatchingContextsList;

    // Iterate contexts
    for (; item != nullptr; item = item->next) {
        // Found matching ID
        if (item->id == this->id) {
            // Unlink Left (Beginning)
            if (prev == nullptr)
                emuenv.adhoc.adhocMatchingContextsList = item->next;

            // Unlink Left (Other)
            else
                prev->next = item->next;
            break;
        }

        // Set Previous Reference
        prev = nullptr;
    }
    delete item;
};

int SceNetAdhocMatchingContext::addTimedFunc(int (*entry)(void *), void *arg, uint64_t timeFromNow) {
    if (!this->calloutSyncing.calloutThreadIsRunning)
        return -1;

    std::lock_guard guard(this->calloutSyncing.calloutMutex);

    auto funcIt = this->calloutSyncing.functions.find(entry);
    if (funcIt != this->calloutSyncing.functions.end())
        return -1; // function is already one the lists

    uint64_t now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();

    auto execAt = now + timeFromNow;

    SceNetAdhocMatchingCalloutFunction func = {
        .execAt = execAt,
        .args = arg,
    };

    this->calloutSyncing.functions.insert({ entry, func });

    return 0;
};

bool SceNetAdhocMatchingContext::searchTimedFunc(int (*entry)(void *)) {
    std::lock_guard guard(this->calloutSyncing.calloutMutex);

    auto funcIt = this->calloutSyncing.functions.find(entry);
    if (funcIt == this->calloutSyncing.functions.end())
        return false; // function is not on the list, nothing do do c:
    return true;
}

int SceNetAdhocMatchingContext::delTimedFunc(int (*entry)(void *)) {
    if (!this->calloutSyncing.calloutThreadIsRunning)
        return -1;

    std::lock_guard guard(this->calloutSyncing.calloutMutex);

    auto funcIt = this->calloutSyncing.functions.find(entry);
    if (funcIt == this->calloutSyncing.functions.end())
        return 0; // function is not on the list, nothing do do c:

    this->calloutSyncing.functions.erase(funcIt->first);

    return 0;
}

void SceNetAdhocMatchingContext::notifyHandler(EmuEnvState *emuenv, int event, SceNetInAddr *peer, int optLen, void *opt) {
    if (!this->handler.entry)
        return;

    if (!this->handler.thread)
        this->handler.thread = emuenv->kernel.create_thread(emuenv->mem, "adhocHandlerThread");

    Ptr<SceNetInAddr> vPeer;
    if (peer) {
        vPeer = Ptr<SceNetInAddr>(alloc(emuenv->mem, sizeof(SceNetInAddr), "adhocHandlerPeer"));
        memcpy(vPeer.get(emuenv->mem), peer, sizeof(*peer));
    } else {
        vPeer = Ptr<SceNetInAddr>(0);
    }

    Ptr<char> vOpt;
    if (opt) {
        vOpt = Ptr<char>(alloc(emuenv->mem, optLen, "adhocHandlerOpt"));
        memcpy(vOpt.get(emuenv->mem), opt, optLen);
    } else {
        vOpt = Ptr<char>(0);
    }

    SceNetAdhocHandlerArguments handleArgs{
        .id = (uint32_t)this->id,
        .event = (uint32_t)event,
        .peer = vPeer.address(),
        .optlen = (uint32_t)optLen,
        .opt = vOpt.address()
    };

    auto data = Ptr<SceNetAdhocHandlerArguments>(alloc(emuenv->mem, sizeof(handleArgs), "handleArgs"));
    memcpy(data.get(emuenv->mem), &handleArgs, sizeof(handleArgs));

    this->handler.thread->run_guest_function(this->handler.entry.address(), sizeof(SceNetAdhocHandlerArguments), data);

    free(emuenv->mem, vPeer); // free peer
    free(emuenv->mem, vOpt); // free opt
    free(emuenv->mem, data); // free arguments
};

void SceNetAdhocMatchingContext::processPacketFromTarget(EmuEnvState *emuenv, SceNetAdhocMatchingTarget *target) {
    SceNetAdhocMatchingPacketType packetType;
    memcpy(&packetType, target->rawPacket + 1, sizeof(packetType));

    SceNetAdhocMatchingMode mode = (SceNetAdhocMatchingMode)this->mode;
    if (mode == SCE_NET_ADHOC_MATCHING_MODE_PARENT) {
        // If we received any of these 2 packets and we are the parent, ignore them, we dont care
        if (packetType == SCE_NET_ADHOC_MATCHING_PACKET_TYPE_HELLO || packetType == SCE_NET_ADHOC_MATCHING_PACKET_TYPE_ADDRS)
            return;
    } else {
        if (mode != SCE_NET_ADHOC_MATCHING_MODE_CHILD) {
            bool peerIsHigherAddrThanUs = false; // TODO: not stub this to false
            if (peerIsHigherAddrThanUs) {
                if (packetType == SCE_NET_ADHOC_MATCHING_PACKET_TYPE_ADDRS)
                    return;
            } else {
                if (packetType == SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK7)
                    return;
            }
        }
    }

    if ((packetType == SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK2 || packetType == SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK3) && target->rawPacketLength - target->packetLength > 15) {
        // TODO
    }

    auto count = this->countTargetsWithStatusOrBetter(3);
    switch (packetType) {
    case SCE_NET_ADHOC_MATCHING_PACKET_TYPE_HELLO:
        if (target->packetLength - 4 > 7) {
            LOG_CRITICAL("Received hello");
            if (target->status == 1) {
                // TODO: something happens here, idk what it is
                if (count + 1 < this->maxnum && this->handler.entry) {
                    if (target->packetLength - sizeof(SceNetAdhocMatchingHelloStart) < 1)
                        this->notifyHandler(emuenv, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_HELLO, &target->addr, 0, nullptr);
                    else
                        this->notifyHandler(emuenv, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_HELLO, &target->addr, target->packetLength - sizeof(SceNetAdhocMatchingHelloStart), target->rawPacket + sizeof(SceNetAdhocMatchingHelloStart));
                }
            }
        }
        break;
    case SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK2:
    case SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK3:
    case SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK4:
    case SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK5:
    case SCE_NET_ADHOC_MATCHING_PACKET_TYPE_ADDRS:
    case SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK7:
    case SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK8:
    case SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK9:
    case SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK10:
    case SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK11: break;
    };

    // TODO
}

void SceNetAdhocMatchingContext::SetTarget68Type(SceNetAdhocMatchingTarget* target, int type) {

}

void SceNetAdhocMatchingContext::SetTargetStatus(SceNetAdhocMatchingTarget* target, SceNetAdhocMatchingTargetStatus status) {

}

SceNetAdhocMatchingTarget *SceNetAdhocMatchingContext::newTarget(uint32_t addr) {
    auto* target = new SceNetAdhocMatchingTarget();

    if (target == nullptr) {
        return nullptr;
    }

    SetTargetStatus(target, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED);
    target->addr.s_addr = addr;
    target->msg = this->msgPipe;
    //SceNetInternal_689B9D7D(&target->unk_58);
    if (target->unk_58 == 0) {
        target->unk_58 = 1;
    }

    target->is_88_pending = false;
    if (target->type_68 != -1) {
        if (target->type_68 == 2 && target->packetLength > 0) {
            delete target->rawPacket;
            target->packetLength = 0;
            target->rawPacket = nullptr;
        }
        target->type_68 = 1;
    }

    target->is_a0_pending = false;
    target->next = targetList;
    targetList = target;

    return target;
}

SceNetAdhocMatchingTarget *SceNetAdhocMatchingContext::findTargetByAddr(uint32_t addr) {
    SceNetAdhocMatchingTarget *target = this->targetList;
    while (target != nullptr) {
        if (target->addr.s_addr == addr && !target->unk_54)
            return target;
        target = target->next;
    }

    return nullptr;
};

void SceNetAdhocMatchingContext::getTargetAddrList(SceNetAdhocMatchingTargetStatus status, SceNetInAddr *addrList, SceSize &addrListSize) {
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
    SceSize i = 0;
    SceNetAdhocMatchingTarget *target;
    for (target = this->targetList; target != nullptr; target = target->next) {
        if (target->status >= status)
            i++;
    }
    return i;
}

bool SceNetAdhocMatchingContext::isTargetAddressHigher(SceNetAdhocMatchingTarget* target) {
    return ownAddress < target->addr.s_addr;
}

int SceNetAdhocMatchingContext::createMembersList() {
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
    SceSize member_count = (memberMsgSize - 8) / sizeof(SceNetInAddr);
    SceSize count = 0;
    
    for (SceSize i = 0; i < member_count; i++) {
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
    int flags = 0x400; // 0x480 if sdk version < 0x1500000
    
    SceNetSockaddrIn addr = {
        .sin_len = sizeof(SceNetSockaddrIn),
        .sin_family = AF_INET,
        .sin_port = htons(0xe4a),
        .sin_addr = target->addr,
        .sin_vport = htons(port),
    };

    auto result = sendto(sendSocket, (char *)memberMsg, memberMsgSize, flags, (sockaddr *)&addr, sizeof(SceNetSockaddrIn));

    if (result == EAGAIN) {
        result = SCE_NET_ADHOC_MATCHING_OK;
    }

    return result; // convert network error to SCE_NET error ?
}

int SceNetAdhocMatchingContext::processMemberPacket(char *packet, SceSize packetLength) {
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


int SceNetAdhocMatchingContext::clearMemberList() {
    if (memberMsgSize == 0) {
        return;
    }
    delete memberMsg;
    memberMsg = nullptr;
    memberMsgSize = 0;
}

int SceNetAdhocMatchingContext::getHelloOpt(SceSize *oOptlen, void *oOpt) {
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
    auto *message = new SceNetAdhocMatchingHelloMessage();

    if (message == nullptr)
        return SCE_NET_ADHOC_MATCHING_ERROR_NO_SPACE;

    message->one = 1;
    message->type = SCE_NET_ADHOC_MATCHING_PACKET_TYPE_HELLO;
    message->packetLength = htons(optlen + 8);
    message->helloInterval = helloInterval;
    message->rexmtInterval = keepAliveInterval;
    message->unk_6c = 1;
    memcpy(message->zero, 0, sizeof(message->zero));

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

int SceNetAdhocMatchingContext::broadcastHello() {
    int flags = 0x400; // 0x480 if sdk version < 0x1500000

    SceNetSockaddrIn addr = {
        .sin_len = sizeof(SceNetSockaddrIn),
        .sin_family = AF_INET,
        .sin_port = htons(0xe4a),
        .sin_addr = INADDR_BROADCAST,
        .sin_vport = htons(port),
    };

    auto result = sendto(this->sendSocket, (char *)this->helloMsg, this->totalHelloLength, flags, (sockaddr *)&addr, sizeof(SceNetSockaddrIn));

    if (result == EAGAIN)
        result = 0;

    return result; // convert network error to SCE_NET error ?
};

void SceNetAdhocMatchingContext::resetHelloOpt() {
    if (this->totalHelloLength == 0)
        return;

    delete this->helloMsg;
    this->helloMsg = nullptr;
    this->totalHelloLength = 0;
}

void SceNetAdhocMatchingContext::resetHelloFunction() {
    if (!shouldHelloReqBeProcessed)
        return;

    // callout::FUN_81002f48(&ctx->callout_thread,&ctx->field_0x90,0);
    shouldHelloReqBeProcessed = false;
}