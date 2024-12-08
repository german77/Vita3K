// Vita3K emulator project
// Copyright (C) 2024 Vita3K team
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

// #include "net/types.h"
#include <module/module.h>

#include <adhoc/state.h>
#include <kernel/state.h>
#include <util/lock_and_find.h>

#include <util/tracy.h>
TRACY_MODULE_NAME(SceNetAdhocMatching);

// s_addr can be a macro on windows
#pragma push_macro("s_addr")
#undef s_addr

EXPORT(int, sceNetAdhocMatchingAbortSendData, int id, SceNetInAddr *addr) {
    TRACY_FUNC(sceNetAdhocMatchingAbortSendData, id, addr);
    if (!emuenv.adhoc.is_initialized)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_NOT_INITIALIZED);

    if (addr == nullptr)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_INVALID_ARG);

    std::lock_guard<std::recursive_mutex> guard(emuenv.adhoc.getMutex());
    SceNetAdhocMatchingContext *ctx = emuenv.adhoc.findMatchingContextById(id);

    if (ctx == nullptr)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_INVALID_ID);

    if (ctx->status != SCE_NET_ADHOC_MATCHING_CONTEXT_STATUS_RUNNING)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_NOT_RUNNING);

    auto *target = ctx->findTargetByAddr(addr->s_addr);

    if (target == nullptr)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_UNKNOWN_TARGET);

    ctx->addA0TimedFunction(emuenv, target);
    ctx->setTargetSendDataStatus(target, 1);

    return SCE_NET_ADHOC_MATCHING_OK;
}

EXPORT(int, sceNetAdhocMatchingCancelTargetWithOpt, int id, SceNetInAddr *target, int optLen, char *opt) {
    TRACY_FUNC(sceNetAdhocMatchingCancelTargetWithOpt, id, target, optLen, opt);
    if (!emuenv.adhoc.is_initialized)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_NOT_INITIALIZED);

    if (target == nullptr)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_INVALID_ARG);

    std::lock_guard<std::recursive_mutex> guard(emuenv.adhoc.getMutex());
    SceNetAdhocMatchingContext *ctx = emuenv.adhoc.findMatchingContextById(id);

    if (ctx == nullptr)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_INVALID_ID);

    if (ctx->status != SCE_NET_ADHOC_MATCHING_CONTEXT_STATUS_RUNNING)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_NOT_RUNNING);

    auto *foundTarget = ctx->findTargetByAddr(target->s_addr);
    if (foundTarget == nullptr)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_UNKNOWN_TARGET);

    if (optLen < 0 || SCE_NET_ADHOC_MATCHING_MAXOPTLEN < optLen)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_INVALID_OPTLEN);

    if (0 < optLen && opt == nullptr)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_INVALID_OPTLEN);

    switch (foundTarget->status) {
    case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED:
        break;
    case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_2:
    case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES:
    case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES2:
    case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED:
        ctx->deleteAllTimedFunctions(emuenv, foundTarget);
        ctx->sendOptDataToTarget(emuenv, thread_id, foundTarget, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_CANCEL, optLen, opt);
        ctx->setTargetStatus(foundTarget, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED);
        if (foundTarget->optLength > 0) {
            delete foundTarget->opt;
            foundTarget->optLength = 0;
            foundTarget->opt = nullptr;
        }
        if (optLen > 0) {
            foundTarget->opt = new char[optLen];
            if (foundTarget->opt == nullptr)
                return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_NO_SPACE);
            memcpy(foundTarget->opt, opt, optLen);
            foundTarget->optLength = optLen;
        }
    }

    return SCE_NET_ADHOC_MATCHING_OK;
}

EXPORT(int, sceNetAdhocMatchingCancelTarget, int id, SceNetInAddr *target) {
    TRACY_FUNC(sceNetAdhocMatchingCancelTarget, id, target);
    return CALL_EXPORT(sceNetAdhocMatchingCancelTargetWithOpt, id, target, 0, nullptr);
}

EXPORT(int, sceNetAdhocMatchingCreate, SceNetAdhocMatchingMode mode, int maxnum, SceUShort16 port, int rxbuflen, unsigned int helloInterval, unsigned int keepaliveInterval, int initCount, unsigned int rexmtInterval, Ptr<void> handlerAddr) {
    TRACY_FUNC(sceNetAdhocMatchingCreate, mode, maxnum, port, rxbuflen, helloInterval, keepaliveInterval, initCount, rexmtInterval, handlerAddr)
    if (!emuenv.adhoc.is_initialized)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_NOT_INITIALIZED);

    std::lock_guard<std::recursive_mutex> guard(emuenv.adhoc.getMutex());

    if (mode < SCE_NET_ADHOC_MATCHING_MODE_PARENT || mode >= SCE_NET_ADHOC_MATCHING_MODE_MAX)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_INVALID_MODE);

    if (maxnum < 2 || maxnum > 16)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_INVALID_MAXNUM);

    if (rxbuflen < maxnum * 4 + 4U)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_RXBUF_TOO_SHORT);

    if ((mode == SCE_NET_ADHOC_MATCHING_MODE_PARENT || mode == SCE_NET_ADHOC_MATCHING_MODE_UDP) && (helloInterval == 0 || keepaliveInterval == 0))
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_INVALID_ARG);

    if (initCount < 0 || rexmtInterval == 0)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_INVALID_ARG);

    const auto id = emuenv.adhoc.createMatchingContext(port);

    if (id < SCE_NET_ADHOC_MATCHING_OK)
        return RET_ERROR(id);

    const auto ctx = emuenv.adhoc.findMatchingContextById(id);
    ctx->mode = mode;

    // Children have 2 peers max (parent and itself)
    ctx->maxnum = 2;
    if (ctx->mode == SCE_NET_ADHOC_MATCHING_MODE_PARENT)
        ctx->maxnum = maxnum;

    ctx->port = port;
    ctx->rxbuflen = rxbuflen;
    ctx->rxbuf = new char[ctx->rxbuflen]; // Reserve space in adhoc

    if (ctx->rxbuf == nullptr) {
        emuenv.adhoc.deleteMatchingContext(ctx);
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_NO_SPACE);
    }

    ctx->helloInterval = helloInterval;
    ctx->keepAliveInterval = keepaliveInterval;
    ctx->retryCount = initCount;
    ctx->rexmtInterval = rexmtInterval;

    ctx->shouldHelloReqBeProcessed = false;
    ctx->helloOptionFlag = 1;
    ctx->targetList = nullptr;

    SceNetAdhocMatchingHandler handler{
        .pc = handlerAddr.address(),
        .thread = thread_id,
    };

    ctx->handler = handler;

    return ctx->id;
}

EXPORT(int, sceNetAdhocMatchingStop, int id) {
    TRACY_FUNC(sceNetAdhocMatchingStop, id);
    if (!emuenv.adhoc.is_initialized)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_NOT_INITIALIZED);

    std::lock_guard<std::recursive_mutex> guard(emuenv.adhoc.getMutex());
    SceNetAdhocMatchingContext *ctx = emuenv.adhoc.findMatchingContextById(id);

    if (ctx == nullptr)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_INVALID_ID);

    // The context is already stopping or its stopped already, nothing to do
    if (ctx->status != SCE_NET_ADHOC_MATCHING_CONTEXT_STATUS_RUNNING)
        return SCE_NET_ADHOC_MATCHING_OK;

    ctx->status = SCE_NET_ADHOC_MATCHING_CONTEXT_STATUS_STOPPING;

    // These 3 may take time because they wait for both threads to end
    ctx->calloutSyncing.closeCalloutThread();
    ctx->closeInputThread(emuenv, thread_id);
   // ctx->closeEventHandler();

    if (ctx->mode == SCE_NET_ADHOC_MATCHING_MODE_PARENT || ctx->mode == SCE_NET_ADHOC_MATCHING_MODE_UDP) {
        ctx->deleteHelloTimedFunction(emuenv);
        ctx->resetHelloOpt();
        ctx->helloPipeMsg.flags &= 0xfffffffe;
    }

    ctx->deleteAllTargets(emuenv, thread_id);
    ctx->clearMemberList();
    ctx->closeSendSocket(emuenv, thread_id);

    ctx->status = SCE_NET_ADHOC_MATCHING_CONTEXT_STATUS_NOT_RUNNING;

    return SCE_NET_ADHOC_MATCHING_OK;
}

EXPORT(int, sceNetAdhocMatchingDelete, int id) {
    TRACY_FUNC(sceNetAdhocMatchingDelete, id);
    if (!emuenv.adhoc.is_initialized)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_NOT_INITIALIZED);

    std::lock_guard<std::recursive_mutex> guard(emuenv.adhoc.getMutex());
    SceNetAdhocMatchingContext *ctx = emuenv.adhoc.findMatchingContextById(id);

    if (ctx == nullptr)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_INVALID_ID);

    if (ctx->status != SCE_NET_ADHOC_MATCHING_CONTEXT_STATUS_NOT_RUNNING)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_IS_RUNNING);

    delete ctx->rxbuf;
    ctx->rxbuf = nullptr;
    emuenv.adhoc.deleteMatchingContext(ctx);

    return SCE_NET_ADHOC_MATCHING_OK;
}

EXPORT(int, sceNetAdhocMatchingGetHelloOpt, int id, SceSize *optlen, void *opt) {
    TRACY_FUNC(sceNetAdhocMatchingGetHelloOpt, id, optlen, opt);

    if (!emuenv.adhoc.is_initialized)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_NOT_INITIALIZED);

    if (optlen == nullptr)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_INVALID_ARG);

    std::lock_guard<std::recursive_mutex> guard(emuenv.adhoc.getMutex());
    SceNetAdhocMatchingContext *ctx = emuenv.adhoc.findMatchingContextById(id);

    if (ctx == nullptr)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_INVALID_ID);

    if (ctx->mode == SCE_NET_ADHOC_MATCHING_MODE_CHILD)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_INVALID_MODE);

    if (ctx->status != SCE_NET_ADHOC_MATCHING_CONTEXT_STATUS_RUNNING)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_CONTEXT_STATUS_NOT_RUNNING);

    return ctx->getHelloOpt(optlen, opt);
}

EXPORT(int, sceNetAdhocMatchingGetMembers, int id, unsigned int *membersCount, SceNetAdhocMatchingMember *members) {
    TRACY_FUNC(sceNetAdhocMatchingGetMembers, id, membersCount, members);
    if (!emuenv.adhoc.is_initialized)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_NOT_INITIALIZED);

    if (membersCount == nullptr)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_INVALID_ARG);

    std::lock_guard<std::recursive_mutex> guard(emuenv.adhoc.getMutex());
    SceNetAdhocMatchingContext *ctx = emuenv.adhoc.findMatchingContextById(id);

    if (ctx == nullptr)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_INVALID_ID);

    if (ctx->status != SCE_NET_ADHOC_MATCHING_CONTEXT_STATUS_RUNNING)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_NOT_RUNNING);

    return ctx->getMembers(membersCount, members);
}

EXPORT(int, sceNetAdhocMatchingSelectTarget, int id, SceNetInAddr *target, int optlen, void *opt) {
    TRACY_FUNC(sceNetAdhocMatchingSelectTarget, id, target, optlen, opt);
    if (!emuenv.adhoc.is_initialized)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_NOT_INITIALIZED);

    if (target == nullptr)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_INVALID_ARG);

    std::lock_guard<std::recursive_mutex> guard(emuenv.adhoc.getMutex());
    SceNetAdhocMatchingContext *ctx = emuenv.adhoc.findMatchingContextById(id);

    if (ctx == nullptr)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_INVALID_ID);

    if (ctx->status != SCE_NET_ADHOC_MATCHING_CONTEXT_STATUS_RUNNING)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_NOT_RUNNING);

    auto foundTarget = ctx->findTargetByAddr(target->s_addr);
    if (foundTarget == nullptr)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_UNKNOWN_TARGET);

    if (optlen < 0 || SCE_NET_ADHOC_MATCHING_MAXOPTLEN < optlen)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_INVALID_OPTLEN);

    if (0 < optlen && opt == nullptr)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_INVALID_OPTLEN);

    auto membersCount = ctx->countTargetsWithStatusOrBetter(SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES);
    switch (foundTarget->status) {
    case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED:
        if (ctx->mode == SCE_NET_ADHOC_MATCHING_MODE_PARENT)
            return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_TARGET_NOT_READY);
        if (membersCount + 1 >= ctx->maxnum)
            return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_EXCEED_MAXNUM);
        if (foundTarget->optLength > 0) {
            delete foundTarget->opt;
            foundTarget->optLength = 0;
            foundTarget->opt = nullptr;
        }
        if (optlen > 0) {
            foundTarget->opt = new char[optlen];
            if (foundTarget->opt == nullptr)
                return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_NO_SPACE);
            memcpy(foundTarget->opt, opt, optlen);
            foundTarget->optLength = optlen;
        }
        foundTarget->targetCount++;
        if (foundTarget->targetCount == 0)
            foundTarget->targetCount = 1;

        ctx->sendOptDataToTarget(emuenv, thread_id, foundTarget, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_HELLO_ACK, foundTarget->optLength, foundTarget->opt);
        ctx->add88TimedFunct(emuenv, foundTarget);
        ctx->setTargetStatus(foundTarget, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES2);
        break;
    case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_2:
        if (ctx->mode == SCE_NET_ADHOC_MATCHING_MODE_PARENT)
            return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_TARGET_NOT_READY);
        if (membersCount + 1 >= ctx->maxnum)
            return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_EXCEED_MAXNUM);
        if (foundTarget->optLength > 0) {
            delete foundTarget->opt;
            foundTarget->optLength = 0;
            foundTarget->opt = nullptr;
        }
        if (optlen > 0) {
            foundTarget->opt = new char[optlen];
            if (foundTarget->opt == nullptr)
                return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_NO_SPACE);
            memcpy(foundTarget->opt, opt, optlen);
            foundTarget->optLength = optlen;
        }
        foundTarget->targetCount++;
        if (foundTarget->targetCount == 0)
            foundTarget->targetCount = 1;

        ctx->sendOptDataToTarget(emuenv, thread_id, foundTarget, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK3, foundTarget->optLength, foundTarget->opt);
        ctx->add88TimedFunct(emuenv, foundTarget);
        ctx->setTargetStatus(foundTarget, SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES2);
        foundTarget->retryCount = ctx->retryCount;
        break;
    case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES:
    case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES2:
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_REQUEST_IN_PROGRESS);
    case SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED:
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_ALREADY_ESTABLISHED);
    }

    return SCE_NET_ADHOC_MATCHING_OK;
}

EXPORT(int, sceNetAdhocMatchingSendData, int id, SceNetInAddr *addr, int dataLen, void *data) {
    TRACY_FUNC(sceNetAdhocMatchingSendData, id, addr, dataLen, data);
    if (!emuenv.adhoc.is_initialized)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_NOT_INITIALIZED);

    if (addr == nullptr)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_INVALID_ARG);

    std::lock_guard<std::recursive_mutex> guard(emuenv.adhoc.getMutex());
    SceNetAdhocMatchingContext *ctx = emuenv.adhoc.findMatchingContextById(id);

    if (ctx == nullptr)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_INVALID_ID);

    auto *target = ctx->findTargetByAddr(addr->s_addr);

    if (target == nullptr)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_UNKNOWN_TARGET);

    if (dataLen < 1 || dataLen > SCE_NET_ADHOC_MATCHING_MAXDATALEN)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_INVALID_DATALEN);

    if (data == nullptr)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_INVALID_ARG);

    if (target->status != SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_NOT_ESTABLISHED);

    if (target->sendDataStatus == 2)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_DATA_BUSY);

    if (target->sendDataStatus == 1) {
        if (target->sendDataLength > 0) {
            delete target->sendData;
            target->sendDataLength = 0;
            target->sendData = nullptr;
        }
        target->sendData = new char[dataLen];

        if (target->sendData == nullptr)
            return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_NO_SPACE);

        memcpy(target->sendData, data, dataLen);
        target->sendDataLength = dataLen;
        target->sendDataCount++;
        ctx->sendDataMessageToTarget(emuenv, thread_id, target, SCE_NET_ADHOC_MATCHING_PACKET_TYPE_DATA, target->sendDataLength, target->sendData);
        ctx->addA0TimedFunction(emuenv, target);
        ctx->setTargetSendDataStatus(target, 2);
    }

    return SCE_NET_ADHOC_MATCHING_OK;
}

EXPORT(int, sceNetAdhocMatchingSetHelloOpt, int id, int optlen, void *opt) {
    TRACY_FUNC(sceNetAdhocMatchingSetHelloOpt, id, optlen, opt);
    if (!emuenv.adhoc.is_initialized)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_NOT_INITIALIZED);

    std::lock_guard<std::recursive_mutex> guard(emuenv.adhoc.getMutex());
    SceNetAdhocMatchingContext *ctx = emuenv.adhoc.findMatchingContextById(id);

    if (ctx == nullptr)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_INVALID_ID);

    if (ctx->mode == SCE_NET_ADHOC_MATCHING_MODE_CHILD)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_INVALID_MODE);

    if (ctx->status != SCE_NET_ADHOC_MATCHING_CONTEXT_STATUS_RUNNING)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_NOT_RUNNING);

    if (optlen < 0 || SCE_NET_ADHOC_MATCHING_MAXHELLOOPTLEN < optlen)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_INVALID_OPTLEN);

    if (0 < optlen && opt == nullptr)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_INVALID_OPTLEN);

    return ctx->setHelloOpt(optlen, opt);
}

EXPORT(int, sceNetAdhocMatchingStart, int id, int threadPriority, int threadStackSize, int threadCpuAffinityMask, int helloOptlen, void *helloOpt) {
    TRACY_FUNC(sceNetAdhocMatchingStart, id, threadPriority, threadStackSize, threadCpuAffinityMask, helloOptlen, helloOpt);
    if (!emuenv.adhoc.is_initialized)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_NOT_INITIALIZED);

    // TODO: Define MAXOPTLEN MAXDATALEN MAXHELLOOPTLEN size limits based on sdk version

    std::lock_guard<std::recursive_mutex> guard(emuenv.adhoc.getMutex());
    SceNetAdhocMatchingContext *ctx = emuenv.adhoc.findMatchingContextById(id);

    if (ctx == nullptr)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_INVALID_ID);

    if (ctx->status != SCE_NET_ADHOC_MATCHING_CONTEXT_STATUS_NOT_RUNNING)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_IS_RUNNING);

    if (helloOptlen < 0 || helloOptlen > SCE_NET_ADHOC_MATCHING_MAXHELLOOPTLEN)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_INVALID_OPTLEN);

    if (helloOptlen > 0 && helloOpt == nullptr)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_INVALID_OPTLEN);

    int result = ctx->initializeSendSocket(emuenv, thread_id);
    if (result != SCE_NET_ADHOC_MATCHING_OK)
        return RET_ERROR(result);

    if (threadPriority == 0)
        threadPriority = 0x10000100;
    if (threadStackSize == 0)
        threadPriority = 0x4000;

    result = ctx->initializeEventHandler(emuenv, thread_id, threadPriority, threadStackSize, threadCpuAffinityMask);
    if (result != SCE_NET_ADHOC_MATCHING_OK) {
        ctx->closeSendSocket(emuenv, thread_id);
        return RET_ERROR(result);
    }

    result = ctx->initializeInputThread(emuenv, thread_id, threadPriority, 0x1000, threadCpuAffinityMask);
    if (result != SCE_NET_ADHOC_MATCHING_OK) {
        ctx->closeEventHandler();
        ctx->closeSendSocket(emuenv, thread_id);
        return RET_ERROR(result);
    }

    result = ctx->calloutSyncing.initializeCalloutThread(emuenv, thread_id, ctx->id, threadPriority, 0x1000, threadCpuAffinityMask);
    if (result != SCE_NET_ADHOC_MATCHING_OK) {
        ctx->closeInputThread(emuenv, thread_id);
        ctx->closeEventHandler();
        ctx->closeSendSocket(emuenv, thread_id);
        return RET_ERROR(result);
    }

    if (ctx->mode == SCE_NET_ADHOC_MATCHING_MODE_PARENT || ctx->mode == SCE_NET_ADHOC_MATCHING_MODE_UDP) {
        result = ctx->setHelloOpt(helloOptlen, helloOpt);
        if (result != SCE_NET_ADHOC_MATCHING_OK) {
            ctx->calloutSyncing.closeCalloutThread();
            ctx->closeInputThread(emuenv, thread_id);
            ctx->closeEventHandler();
            ctx->closeSendSocket(emuenv, thread_id);
            return RET_ERROR(result);
        }

        ctx->addHelloTimedFunct(emuenv, ctx->helloInterval);
    }

    ctx->createMembersList();

    ctx->status = SCE_NET_ADHOC_MATCHING_CONTEXT_STATUS_RUNNING;

    return SCE_NET_ADHOC_MATCHING_OK;
}

EXPORT(int, sceNetAdhocMatchingInit, SceSize poolsize, void *poolptr) {
    TRACY_FUNC(sceNetAdhocMatchingInit, poolsize, poolptr);
    if (emuenv.adhoc.is_initialized)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_ALREADY_INITIALIZED);

    if (poolptr == nullptr)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_INVALID_ARG);

    int result = emuenv.adhoc.initializeMutex();
    if (result != SCE_NET_ADHOC_MATCHING_OK)
        return RET_ERROR(result);

    if (poolsize == 0) {
        poolsize = 0x20000;
    }

    result = emuenv.adhoc.createMSpace(poolsize, poolptr);
    if (result != SCE_NET_ADHOC_MATCHING_OK) {
        emuenv.adhoc.deleteMutex();
        return RET_ERROR(result);
    }

    result = emuenv.adhoc.initializeMatchingContextList();
    if (result != SCE_NET_ADHOC_MATCHING_OK) {
        emuenv.adhoc.deleteMSpace();
        emuenv.adhoc.deleteMutex();
        return RET_ERROR(result);
    }

    emuenv.adhoc.is_initialized = true;

    return SCE_NET_ADHOC_MATCHING_OK;
}

EXPORT(int, sceNetAdhocMatchingTerm) {
    TRACY_FUNC(sceNetAdhocMatchingTerm);
    if (!emuenv.adhoc.is_initialized)
        return RET_ERROR(SCE_NET_ADHOC_MATCHING_ERROR_NOT_INITIALIZED);

    for (int i = 0; i < SCE_NET_ADHOC_MATCHING_MAXNUM; i++) {
        // This call will use the mutex
        CALL_EXPORT(sceNetAdhocMatchingStop, i);

        // We aren't guarded by a mutex. We need to check every iteration
        if (!emuenv.adhoc.is_initialized)
            continue;

        std::lock_guard<std::recursive_mutex> guard(emuenv.adhoc.getMutex());
        auto ctx = emuenv.adhoc.findMatchingContextById(i);
        if (ctx == nullptr)
            continue;
        if (ctx->status != SCE_NET_ADHOC_MATCHING_CONTEXT_STATUS_NOT_RUNNING)
            continue;

        delete ctx->rxbuf;
        ctx->rxbuf = nullptr;
        emuenv.adhoc.deleteMatchingContext(ctx);
    }

    int result = emuenv.adhoc.isAnyMatchingContextRunning();
    if (result != SCE_NET_ADHOC_MATCHING_OK) {
        return RET_ERROR(result);
    }

    emuenv.adhoc.deleteAllMatchingContext();
    emuenv.adhoc.deleteMSpace();
    emuenv.adhoc.deleteMutex();
    emuenv.adhoc.is_initialized = false;
    return SCE_NET_ADHOC_MATCHING_OK;
}

#pragma pop_macro("s_addr")
