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

#include <mutex>

#include "adhoc/calloutSyncing.h"
#include "adhoc/matchingContext.h"
#include "adhoc/matchingTarget.h"
#include "adhoc/state.h"
#include "adhoc/threads.h"
#include "net/types.h"
#include "util/log.h"

#include "../SceNet/SceNet.h"

int adhocMatchingEventThread(EmuEnvState &emuenv, SceUID thread_id, SceUID id) {
    auto ctx = emuenv.adhoc.findMatchingContextById(id);

    SceNetAdhocMatchingPipeMessage pipeMessage;
    while (ctx->isRunning()) {
        if (read(ctx->getReadPipeUid(), &pipeMessage, sizeof(pipeMessage)) < 0)
            return 0;
        if (pipeMessage.type == SCE_NET_ADHOC_MATCHING_EVENT_ABORT) {
            ctx->broadcastAbort(emuenv, thread_id);
            return 0;
        }

        //std::lock_guard<std::mutex> guard(emuenv.adhoc.getMutex());
        LOG_INFO("event: {}", (int)pipeMessage.type);

        switch (pipeMessage.type) {
        case SCE_NET_ADHOC_MATCHING_EVENT_PACKET:
            ctx->handleEventMessage(emuenv, thread_id, pipeMessage.target);
            break;
        case SCE_NET_ADHOC_MATCHING_EVENT_REGISTRATION_TIMEOUT:
            ctx->handleEventRegistrationTimeout(emuenv, thread_id, pipeMessage.target);
            break;
        case SCE_NET_ADHOC_MATCHING_EVENT_TARGET_TIMEOUT:
            ctx->handleEventTargetTimeout(emuenv, thread_id, pipeMessage.target);
            break;
        case SCE_NET_ADHOC_MATCHING_EVENT_HELLO_TIMEOUT:
            ctx->handleEventHelloTimeout(emuenv, thread_id);
            break;
        case SCE_NET_ADHOC_MATCHING_EVENT_DATA_TIMEOUT:
            ctx->handleEventDataTimeout(emuenv, thread_id, pipeMessage.target);
            break;
        }

        if (pipeMessage.target == nullptr) {
            continue;
        }

        if (pipeMessage.target->delete_target && !pipeMessage.target->incomingPacketMessage.isSheduled && !pipeMessage.target->targetTimeout.message.isSheduled) {
            ctx->deleteTarget(pipeMessage.target);
        }
    }

    return 0;
};

int adhocMatchingInputThread(EmuEnvState &emuenv, SceUID thread_id, SceUID id) {
    auto ctx = emuenv.adhoc.findMatchingContextById(id);

    SceNetSockaddrIn fromAddr{};
    unsigned int fromAddrLen = sizeof(SceNetSockaddrIn);

    while (ctx->isRunning()) {
        int rawPacketSize;
        SceNetAdhocMatchingMessageHeader packet;

        // Wait until valid data arrives
        while (ctx->isRunning()) {
            rawPacketSize = CALL_EXPORT(sceNetRecvfrom, ctx->recvSocket, ctx->rxbuf, ctx->rxbuflen, 0, (SceNetSockaddr *)&fromAddr, &fromAddrLen);
            if (rawPacketSize < SCE_NET_ADHOC_MATCHING_OK)
                return 0;
            if (rawPacketSize < sizeof(SceNetAdhocMatchingMessageHeader))
                continue;
            if (fromAddr.sin_addr.s_addr == ctx->ownAddress && fromAddr.sin_port == ctx->ownPort)
                continue;

            packet.parse(ctx->rxbuf, rawPacketSize);
            if (packet.one != 1)
                continue;
            if (rawPacketSize >= packet.packetLength + sizeof(SceNetAdhocMatchingMessageHeader))
                break;
        }

        if (!ctx->isRunning())
            return 0;

        uint8_t addr[4];
        memcpy(addr, &fromAddr.sin_addr.s_addr, 4);
        std::string data = "";
        for (std::size_t i = 0; i < rawPacketSize; i++) {
            data = fmt::format("{} {:x}", data, ((uint8_t *)ctx->rxbuf)[i]);
        }
        LOG_INFO("New input from {}.{}.{}.{}:{}={}", addr[0], addr[1], addr[2], addr[3], htons(fromAddr.sin_port), data);

        // We received the whole packet, we can now commence the parsing and the fun
        std::lock_guard<std::mutex> guard(emuenv.adhoc.getMutex());
        ctx->handleIncommingPackage(&fromAddr.sin_addr, rawPacketSize, packet.packetLength);
    }

    return 0;
};

int adhocMatchingCalloutThread(EmuEnvState &emuenv, SceUID id) {
    auto ctx = emuenv.adhoc.findMatchingContextById(id);
    auto &calloutSyncing = ctx->getCalloutSyncing();

    while (calloutSyncing.isRunning()) {
        int sleepTime = calloutSyncing.excecuteTimedFunctions();

        // Limit sleep time to something reasonable
        if (sleepTime <= 0) {
            sleepTime = 1;
        }
        if (sleepTime > 50) {
            sleepTime = 50;
        }

        // TODO use ctx->calloutSyncing.condvar to break from this sleep early
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepTime));
    }

    return 0;
};
