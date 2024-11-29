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

int sendHelloReqToPipe(void *arg) {
    SceNetAdhocMatchingContext *ctx = (SceNetAdhocMatchingContext *)arg;
    if ((ctx->helloPipeMsg.flags & 1U) == 0) {
        ctx->helloPipeMsg.type = SCE_NET_ADHOC_MATCHING_EVENT_HELLO_SEND;
        ctx->helloPipeMsg.flags = ctx->helloPipeMsg.flags | 1;
        ctx->helloPipeMsg.peer = nullptr;
        //write(ctx->pipesFd[1], &ctx->helloPipeMsg, sizeof(ctx->helloPipeMsg));
    }
    return 0;
}

int adhocMatchingEventThread(EmuEnvState *emuenv, SceUID thread_id, int id) {
    tracy::SetThreadName("adhocMatchingEventThread");
    auto ctx = emuenv->adhoc.findMatchingContextById(id);

    SceNetAdhocMatchingPipeMessage pipeMessage;
    while (read(ctx->msgPipeUid[0], &pipeMessage, sizeof(pipeMessage)) >= 0) {
        ZoneScopedC(0xFFC2C6);
        std::lock_guard<std::mutex> guard(emuenv->adhoc.getMutex());
        
        int type = pipeMessage.type;
        auto target = pipeMessage.peer;
        pipeMessage.flags &= ~1U; // get rid of last bit

        switch (type) {
        case SCE_NET_ADHOC_MATCHING_EVENT_PACKET: { // Packet received
            target->pipeMsg28.flags &= ~1U;
            ctx->processPacketFromTarget(emuenv, target);
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
            // TODO
            break;
        }
        case SCE_NET_ADHOC_MATCHING_EVENT_HELLO_SEND: { // broadcast hello message to network
            int num = ctx->countTargetsWithStatusOrBetter(SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES);
            // also count ourselves
            if (num + 1 < ctx->maxnum)
                ctx->broadcastHello(*emuenv, thread_id);

            //if (ctx->searchTimedFunc(sendHelloReqToPipe)) {
            //    ctx->delTimedFunc(sendHelloReqToPipe); // not run it, we are gonna reschedule it
            //};
            //ctx->addTimedFunc(sendHelloReqToPipe, ctx, ctx->helloInterval);
            ctx->helloPipeMsg.flags &= ~1U;
            break;
        }
        case SCE_NET_ADHOC_MATCHING_EVENT_UNK5: {
            // TODO
            break;
        }
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
            } while (fromAddr->sin_addr.s_addr == ctx->ownAddress || *ctx->rxbuf != 1);
            // we have a packet :D, but may be unfinished, check how long it is and see if we can get the remaining

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
    auto ctx = emuenv->adhoc.findMatchingContextById(id);

    do {
        ctx->calloutSyncing.calloutMutex.lock();

        auto &list = ctx->calloutSyncing.functions;
        uint64_t now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        for (auto &func : list) {
            if (func.second.execAt > now)
                continue;

            // LOG_CRITICAL("running timed func");
            ctx->calloutSyncing.calloutMutex.unlock();
            (func.first)(func.second.args);
            ctx->calloutSyncing.calloutMutex.lock();
            // running callout func
        };
        // TODO: should something be done here?
        ctx->calloutSyncing.calloutMutex.unlock();
    } while (ctx->calloutSyncing.calloutShouldExit == false);
    LOG_CRITICAL("calloutShouldExit:{}", ctx->calloutSyncing.calloutShouldExit);

    return 0;
};
