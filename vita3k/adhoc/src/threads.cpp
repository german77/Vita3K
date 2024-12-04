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

#include <adhoc/state.h>
#include <net/types.h>
#include "../SceNet/SceNet.h"
#include <chrono>
#include <mutex>

#include <util/tracy.h>
TRACY_MODULE_NAME(SceNetAdhocMatching);

int adhocMatchingEventThread(EmuEnvState *emuenv, SceUID thread_id, int id) {
    tracy::SetThreadName("adhocMatchingEventThread");
    auto ctx = emuenv->adhoc.findMatchingContextById(id);

    SceNetAdhocMatchingPipeMessage pipeMessage;
    while (read(ctx->msgPipeUid[0], &pipeMessage, sizeof(pipeMessage)) >= 0) {
        LOG_CRITICAL("GOT SOME EVENTS");
        ZoneScopedC(0xFFC2C6);
        std::lock_guard<std::mutex> guard(emuenv->adhoc.getMutex());
        
        int type = pipeMessage.type;
        auto target = pipeMessage.peer;
        pipeMessage.flags &= ~1U; // get rid of last bit

        switch (type) {
        case SCE_NET_ADHOC_MATCHING_EVENT_PACKET: { // Packet received
            target->pipeMsg88.flags &= ~1U;
            ctx->processPacketFromTarget(*emuenv, target);
            if (target->rawPacket)
                delete target->rawPacket;
            target->rawPacket = 0;
            target->rawPacketLength = 0;
            break;
        }
        case SCE_NET_ADHOC_MATCHING_EVENT_UNK2: { // idk
            // TODO
            break;
        }
        case SCE_NET_ADHOC_MATCHING_EVENT_UNK3: { // idk
            target->pipeMsg88.flags = (target->pipeMsg88).flags & 0xfffffffe;
            if (target->status != SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED) {
                break;
            }
            target->uuid2++;
            if (target->uuid2 < 1) {
                ctx->setTargetStatus(target, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED);
                ctx->sendOptDataToTarget(*emuenv, thread_id, target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK5, 0, nullptr);
                ctx->notifyHandler(emuenv, ctx->id, 8, &target->addr, 0, nullptr);
            } else {
                if (ctx->mode == SCE_NET_ADHOC_MATCHING_MODE_PARENT || (ctx->mode == SCE_NET_ADHOC_MATCHING_MODE_UDP && ctx->isTargetAddressHigher(target))) {
                    ctx->sendMemberListToTarget(target);
                }
                ctx->add88TimedFunctionWithParentInterval(*emuenv, target);
            }
            break;
        }
        case SCE_NET_ADHOC_MATCHING_EVENT_HELLO_SEND: { // broadcast hello message to network
            ctx->helloPipeMsg.flags &= 0xfffffffe;
            int num = ctx->countTargetsWithStatusOrBetter(SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES);
            // also count ourselves
            int result = 0;
            if (num + 1 < ctx->maxnum)
                result = ctx->broadcastHello(*emuenv, thread_id);

            LOG_INFO("result hellow {}",result);
            ctx->addHelloTimedFunct(*emuenv, ctx->helloInterval);
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
                ctx->notifyHandler(emuenv, ctx->id, 0xd, &target->addr, 0, nullptr);
            }
            break;
        }
        }

        if (target != nullptr && target->unk_54 == 1 && (target->pipeMsg28.flags & 1) == 0 && (target->pipeMsg88.flags & 1) == 0) {
            ctx->deleteTarget(target);
        }
    }

    return 0;
};

int adhocMatchingInputThread(EmuEnvState *emuenvn, SceUID thread_id, int id) {
    tracy::SetThreadName("adhocMatchingInputThread");
    auto &emuenv = *emuenvn;
    auto ctx = emuenv.adhoc.findMatchingContextById(id);

    SceNetSockaddrIn *fromAddr{};
    unsigned int fromAddrLen = sizeof(SceNetSockaddrIn);

    while (true) {
        ZoneScopedC(0xFFC2C6);
        int res;
        SceUShort16 packetLength{};
        do {
            do {
                res = CALL_EXPORT(sceNetRecvfrom, ctx->recvSocket, ctx->rxbuf, ctx->rxbuflen, 0, (SceNetSockaddr *)fromAddr, &fromAddrLen);
                if (res < SCE_NET_ADHOC_MATCHING_OK) {
                    assert(false);
                    return 0; // exit the thread immediatly
                }

                // Ignore packets of our own (own broadcast) and make sure the first 4 bytes is host byte order 1

                LOG_CRITICAL("SOME INPUT");
            } while (fromAddr->sin_addr.s_addr == ctx->ownAddress || *ctx->rxbuf != 1);
            // we have a packet :D, but may be unfinished, check how long it is and see if we can get the remaining

            LOG_CRITICAL("WE GOT DATA");
            SceUShort16 nPacketLength; // network byte order of packet length
            memcpy(&nPacketLength, ctx->rxbuf + 2, 2);
            SceUShort16 packetLenght = ntohs(nPacketLength); // ACTUALLY the packet length fr this time
        } while (res < packetLength + 4);
        // We received the whole packet, we can now commence the parsing and the fun
        std::lock_guard<std::mutex> guard(emuenv.adhoc.getMutex());
        auto foundTarget = ctx->findTargetByAddr(fromAddr->sin_addr.s_addr);
        if (foundTarget == nullptr) {
            uint8_t targetMode = *(ctx->rxbuf + 1);
            if (((targetMode == SCE_NET_ADHOC_MATCHING_MODE_CHILD) && ((ctx->mode == SCE_NET_ADHOC_MATCHING_MODE_PARENT || ctx->mode == SCE_NET_ADHOC_MATCHING_MODE_UDP))) || ((targetMode == SCE_NET_ADHOC_MATCHING_MODE_PARENT && ((ctx->mode == SCE_NET_ADHOC_MATCHING_MODE_CHILD || (ctx->mode == SCE_NET_ADHOC_MATCHING_MODE_UDP)))))) {
                foundTarget = ctx->newTarget(fromAddr->sin_addr.s_addr);
            }

            if ((foundTarget->pipeMsg28.flags & 1U) == 0) {
                auto rawPacket = new char[res];
                if (rawPacket == nullptr)
                    continue;

                // Copy the whole packet we received into the peer
                memcpy(rawPacket, ctx->rxbuf, res);
                foundTarget->rawPacketLength = res;
                foundTarget->rawPacket = rawPacket;
                foundTarget->packetLength = packetLength + 4;
                foundTarget->keepAliveInterval = ctx->keepAliveInterval;

                foundTarget->pipeMsg28.type = SCE_NET_ADHOC_MATCHING_EVENT_PACKET;
                foundTarget->pipeMsg28.peer = foundTarget;
                foundTarget->pipeMsg28.flags = foundTarget->pipeMsg28.flags | 1;
                write(ctx->msgPipeUid[1], &foundTarget->pipeMsg28, sizeof(foundTarget->pipeMsg28));
            }
        }
    }

    return 0;
};

int adhocMatchingCalloutThread(EmuEnvState *emuenv, int id) {
    tracy::SetThreadName("adhocMatchingCalloutThread");
    auto ctx = emuenv->adhoc.findMatchingContextById(id);

    do {
        ZoneScopedC(0xFFC2C6);
        ctx->calloutSyncing.mutex.lock();

        auto *entry = ctx->calloutSyncing.functionList;
        uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();

        while (entry != nullptr && entry->execAt < now) {
            LOG_CRITICAL("CALL SOMETHING DELAYED");
            ctx->calloutSyncing.mutex.unlock();
            entry->function(entry->args);
            ctx->calloutSyncing.mutex.lock();

            ctx->calloutSyncing.functionList = entry->next;
            entry = ctx->calloutSyncing.functionList;
        }

        if (ctx->calloutSyncing.shouldExit) {
            break;
        }

        uint64_t sleep_time = 0;
        if (entry != nullptr) {
            sleep_time = entry->execAt - now;
        }

        ctx->calloutSyncing.mutex.unlock();
       
        // Limit sleep time to something reasonable
        if (sleep_time <= 0) {
            sleep_time = 5;
        }
        if (sleep_time > 500) {
            sleep_time = 500;
        }

        // TODO use ctx->calloutSyncing.condvar to break from this sleep early
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
    } while (ctx->calloutSyncing.shouldExit == false);

    LOG_CRITICAL("calloutShouldExit:{}", ctx->calloutSyncing.shouldExit);

    return 0;
};
