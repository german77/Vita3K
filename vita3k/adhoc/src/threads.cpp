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

#include "module/module.h"
#include <kernel/state.h>

#include "../SceNet/SceNet.h"
#include <adhoc/state.h>
#include <chrono>
#include <mutex>
#include <net/types.h>

#include <util/tracy.h>
TRACY_MODULE_NAME(SceNetAdhocMatching);

int adhocMatchingEventThread(EmuEnvState &emuenv, int id) {
    tracy::SetThreadName("adhocMatchingEventThread");
    auto ctx = emuenv.adhoc.findMatchingContextById(id);
    SceUID thread_id = ctx->event_thread_id;

    SceNetAdhocMatchingPipeMessage pipeMessage;
    while (read(ctx->msgPipeUid[0], &pipeMessage, sizeof(pipeMessage)) >= 0) {
        ZoneScopedC(0xFFC2C6);
        std::lock_guard<std::recursive_mutex> guard(emuenv.adhoc.getMutex());

        int type = pipeMessage.type;
        auto target = pipeMessage.peer;
        pipeMessage.flags &= 0xfffffffe; // get rid of last bit

        LOG_INFO("event: {}", type);
        switch (type) {
        case SCE_NET_ADHOC_MATCHING_EVENT_ABORT:
            return 0;
        case SCE_NET_ADHOC_MATCHING_EVENT_PACKET: { // Packet received
            target->pipeMsg28.flags &= 0xfffffffe;
            ctx->processPacketFromTarget(emuenv, thread_id, target);
            if (target->rawPacket)
                delete target->rawPacket;
            target->rawPacket = 0;
            target->rawPacketLength = 0;
            break;
        }
        case SCE_NET_ADHOC_MATCHING_EVENT_UNK2: {
            target->pipeMsg88.flags &= ~1U;
            if (target->status == SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES2) {
                if (target->retryCount > 0) {
                    target->retryCount--;
                }
                if (target->unk_50 || target->retryCount > 0) {
                    ctx->sendOptDataToTarget(emuenv, thread_id, target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_HELLO_ACK, target->optLength, target->opt);
                    ctx->add88TimedFunct(emuenv, target);
                } else {
                    ctx->setTargetStatus(target, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED);
                    ctx->sendOptDataToTarget(emuenv, thread_id, target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_CANCEL, 0, nullptr);
                    ctx->notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_TIMEOUT, &target->addr, 0, nullptr);
                }
            }
            if (target->status == SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES) {
                target->retryCount++;
                if (target->retryCount < 1) {
                    ctx->setTargetStatus(target, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED);
                    ctx->sendOptDataToTarget(emuenv, thread_id, target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_CANCEL, 0, nullptr);
                    ctx->notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_TIMEOUT, &target->addr, 0, nullptr);
                } else {
                    ctx->sendOptDataToTarget(emuenv, thread_id, target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK3, target->optLength, target->opt);
                    ctx->add88TimedFunct(emuenv, target);
                }
            }
            break;
        }
        case SCE_NET_ADHOC_MATCHING_EVENT_UNK3: {
            target->pipeMsg88.flags = (target->pipeMsg88).flags & 0xfffffffe;
            if (target->status != SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED) {
                break;
            }
            target->retryCount2++;
            if (target->retryCount2 < 1) {
                ctx->setTargetStatus(target, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED);
                ctx->sendOptDataToTarget(emuenv, thread_id, target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_CANCEL, 0, nullptr);
                ctx->notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_TIMEOUT, &target->addr, 0, nullptr);
            } else {
                if (ctx->mode == SCE_NET_ADHOC_MATCHING_MODE_PARENT || (ctx->mode == SCE_NET_ADHOC_MATCHING_MODE_UDP && ctx->isTargetAddressHigher(target))) {
                    ctx->sendMemberListToTarget(target);
                }
                ctx->add88TimedFunctionWithParentInterval(emuenv, target);
            }
            break;
        }
        case SCE_NET_ADHOC_MATCHING_EVENT_HELLO_SEND: { // broadcast hello message to network
            ctx->helloPipeMsg.flags &= 0xfffffffe;
            int num = ctx->countTargetsWithStatusOrBetter(SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES);
            // also count ourselves
            int result = 0;
            if (num + 1 < ctx->maxnum)
                result = ctx->broadcastHello(emuenv, thread_id);

            ctx->addHelloTimedFunct(emuenv, ctx->helloInterval);
            break;
        }
        case SCE_NET_ADHOC_MATCHING_EVENT_UNK5: {
            target->msgA0.flags &= 0xfffffffe;
            if (target->sendDataStatus != 2) {
                break;
            }
            target->context_uuid++;
            if (target->context_uuid < 1) {
                ctx->setTargetSendDataStatus(target, 1);
                ctx->notifyHandler(emuenv, thread_id, SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_DATA_TIMEOUT, &target->addr, 0, nullptr);
            }
            break;
        }
        }

        if (target != nullptr && target->delete_target && (target->pipeMsg28.flags & 1) == 0 && (target->pipeMsg88.flags & 1) == 0) {
            ctx->deleteTarget(target);
        }
    }

    return 0;
};

int adhocMatchingInputThread(EmuEnvState &emuenv, int id) {
    tracy::SetThreadName("adhocMatchingInputThread");
    auto ctx = emuenv.adhoc.findMatchingContextById(id);
    SceUID thread_id = ctx->input_thread_id;

    SceNetSockaddrIn fromAddr{};
    unsigned int fromAddrLen = sizeof(SceNetSockaddrIn);

    while (true) {
        ZoneScopedC(0xFFC2C6);
        int res;
        SceUShort16 packetLength{};
        do {
            do {
                res = CALL_EXPORT(sceNetRecvfrom, ctx->recvSocket, ctx->rxbuf, ctx->rxbuflen, 0, (SceNetSockaddr *)&fromAddr, &fromAddrLen);
                if (res < SCE_NET_ADHOC_MATCHING_OK) {
                    return 0; // exit the thread immediatly
                }
            } while (*ctx->rxbuf != 1);

            uint8_t addr[4];
            memcpy(addr, &fromAddr.sin_addr.s_addr, 4);
            // Ignore packets of our own (own broadcast) and make sure the first 4 bytes is host byte order 1
            if (fromAddr.sin_addr.s_addr == ctx->ownAddress && fromAddr.sin_port == ctx->ownPort) {
                // continue;
            }

            std::string data = std::string(ctx->rxbuf, res);
            LOG_INFO("New input from {}.{}.{}.{}:{}={}", addr[0], addr[1], addr[2], addr[3], htons(fromAddr.sin_port), data);

            SceUShort16 nPacketLength; // network byte order of packet length
            memcpy(&nPacketLength, ctx->rxbuf + 2, 2);
            packetLength = ntohs(nPacketLength); // ACTUALLY the packet length fr this time
        } while (res < packetLength + 4);
        // We received the whole packet, we can now commence the parsing and the fun
        std::lock_guard<std::recursive_mutex> guard(emuenv.adhoc.getMutex());
        auto target = ctx->findTargetByAddr(fromAddr.sin_addr.s_addr);

        // No target found try to create one
        if (target == nullptr) {
            SceNetAdhocMatchingPacketType type = *(SceNetAdhocMatchingPacketType *)(ctx->rxbuf + 1);
            if (type == SCE_NET_ADHOC_MATCHING_PACKET_TYPE_HELLO_ACK && (ctx->mode == SCE_NET_ADHOC_MATCHING_MODE_PARENT || ctx->mode == SCE_NET_ADHOC_MATCHING_MODE_UDP)) {
                target = ctx->newTarget(fromAddr.sin_addr.s_addr);
            }
            if (type == SCE_NET_ADHOC_MATCHING_PACKET_TYPE_HELLO && (ctx->mode == SCE_NET_ADHOC_MATCHING_MODE_CHILD || ctx->mode == SCE_NET_ADHOC_MATCHING_MODE_UDP)) {
                target = ctx->newTarget(fromAddr.sin_addr.s_addr);
            }
        }

        // No target available
        if (target == nullptr) {
            continue;
        }

        if ((target->pipeMsg28.flags & 1U) == 0) {
            auto rawPacket = new char[res];
            if (rawPacket == nullptr)
                continue;

            // Copy the whole packet we received into the peer
            memcpy(rawPacket, ctx->rxbuf, res);
            target->rawPacketLength = res;
            target->rawPacket = rawPacket;
            target->packetLength = packetLength + 4;
            target->keepAliveInterval = ctx->keepAliveInterval;

            target->pipeMsg28.type = SCE_NET_ADHOC_MATCHING_EVENT_PACKET;
            target->pipeMsg28.peer = target;
            target->pipeMsg28.flags |= 1;
            write(ctx->msgPipeUid[1], &target->pipeMsg28, sizeof(target->pipeMsg28));
        }
    }
    return 0;
};

int adhocMatchingCalloutThread(EmuEnvState &emuenv, int id) {
    tracy::SetThreadName("adhocMatchingCalloutThread");
    auto ctx = emuenv.adhoc.findMatchingContextById(id);

    do {
        ZoneScopedC(0xFFC2C6);
        ctx->calloutSyncing.mutex.lock();

        auto *entry = ctx->calloutSyncing.functionList;
        uint64_t now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();

        while (entry != nullptr && entry->execAt < now) {
            ctx->calloutSyncing.mutex.unlock();
            entry->function(entry->args);
            ctx->calloutSyncing.mutex.lock();

            ctx->calloutSyncing.functionList = entry->next;
            entry = ctx->calloutSyncing.functionList;
        }

        if (ctx->calloutSyncing.shouldExit) {
            ctx->calloutSyncing.mutex.unlock();
            break;
        }

        uint64_t sleep_time = 0;
        if (entry != nullptr) {
            sleep_time = entry->execAt - now;
        }

        ctx->calloutSyncing.mutex.unlock();

        // Limit sleep time to something reasonable
        if (sleep_time <= 0) {
            sleep_time = 1;
        }
        if (sleep_time > 500) {
            sleep_time = 500;
        }

        // TODO use ctx->calloutSyncing.condvar to break from this sleep early
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
    } while (ctx->calloutSyncing.shouldExit == false);

    return 0;
};
