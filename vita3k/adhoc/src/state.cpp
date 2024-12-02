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

#include <adhoc/state.h>
#include <util/tracy.h>
TRACY_MODULE_NAME(SceNetAdhocMatching);

int AdhocState::initializeMutex(EmuEnvState &emuenv) {
    ZoneScopedC(0xF6C2FF); // Tracy - Track function scope with color thistle
    // Initialize mutex
    is_mutex_initialized = true;
    FrameMarkNamed("Adhoc");
    return SCE_NET_ADHOC_MATCHING_OK;
}

int AdhocState::deleteMutex(EmuEnvState &emuenv) {
    ZoneScopedC(0xF6C2FF); // Tracy - Track function scope with color thistle
    if (is_mutex_initialized) {
        // Delete mutex
    }
    is_mutex_initialized = false;
    FrameMarkNamed("Adhoc");
    return SCE_NET_ADHOC_MATCHING_OK;
}

std::mutex &AdhocState::getMutex(EmuEnvState &emuenv) {
    ZoneScopedC(0xF6C2FF); // Tracy - Track function scope with color thistle
    return mutex;
}

int AdhocState::createMSpace(EmuEnvState &emuenv,SceSize poolsize, void *poolptr) {
    ZoneScopedC(0xF6C2FF);
    // Just a placeholder. We don't really need this kind of allocation
    return SCE_NET_ADHOC_MATCHING_OK;
}

int AdhocState::deleteMSpace(EmuEnvState &emuenv) {
    tracy::SetThreadName("deleteMSpace");
    ZoneScopedC(0xF6C2FF);
    // Just a placeholder. We don't really need this kind of allocation
    return SCE_NET_ADHOC_MATCHING_OK;
}

int AdhocState::initializeMatchingContextList(EmuEnvState &emuenv) {
    ZoneScopedC(0xF6C2FF);
    contextList = nullptr;
    return SCE_NET_ADHOC_MATCHING_OK;
}

int AdhocState::isAnyMatchingContextRunning(EmuEnvState &emuenv) {
    ZoneScopedC(0xF6C2FF);
    SceNetAdhocMatchingContext *context = contextList;
    for (; context != nullptr; context = context->next) {
        if (context->status != SCE_NET_ADHOC_MATCHING_CONTEXT_STATUS_NOT_RUNNING)
            return SCE_NET_ADHOC_MATCHING_ERROR_BUSY;
    }
    return SCE_NET_ADHOC_MATCHING_OK;
}

SceNetAdhocMatchingContext *AdhocState::findMatchingContextById(EmuEnvState &emuenv,int id) {
    ZoneScopedC(0xF6C2FF);
    // Iterate Matching Context List
    SceNetAdhocMatchingContext *context = contextList;
    for (; context != nullptr; context = context->next) {
        return context;
        if (context->id != id)
            continue;
        return context;
    }

    // Context not found
    return nullptr;
};

int AdhocState::createMatchingContext(EmuEnvState &emuenv,SceUShort16 port) {
    ZoneScopedC(0xF6C2FF);
    SceNetAdhocMatchingContext *context = contextList;

    // Check for port conflicts
    for (; context != nullptr; context = context->next) {
         if (context->port != port)
            continue;
        return SCE_NET_ADHOC_MATCHING_ERROR_PORT_IN_USE;
    }

    int next_id = 1;
    if (matchingCtxCount != SCE_NET_ADHOC_MATCHING_MAXNUM - 1)
        next_id = matchingCtxCount + 1;

    do {
        // We did a full loop. There are no id available.
        if (next_id == matchingCtxCount) {
            return SCE_NET_ADHOC_MATCHING_ERROR_ID_NOT_AVAIL;
        }

        context = findMatchingContextById(emuenv, next_id);

        // This id is already in use. Find next id.
        if (context != nullptr) {
            next_id++;
            if (next_id >= SCE_NET_ADHOC_MATCHING_MAXNUM) {
                next_id = 1;
            }
            continue;
        }

        // Return if an error occured
        if (next_id < SCE_NET_ADHOC_MATCHING_OK) {
            matchingCtxCount = next_id;
            return next_id;
        }

        matchingCtxCount = next_id;
        const auto ctx = new SceNetAdhocMatchingContext();

        if (ctx == nullptr) {
            return SCE_NET_ADHOC_MATCHING_ERROR_NO_SPACE;
        }

        // Add new element to the list
        ctx->id = next_id;
        ctx->next = contextList;
        contextList = ctx;
        return next_id;
    } while (true);
}

void AdhocState::deleteMatchingContext(EmuEnvState &emuenv,SceNetAdhocMatchingContext *ctx) {
    ZoneScopedC(0xF6C2FF);
    SceNetAdhocMatchingContext *context = contextList;
    SceNetAdhocMatchingContext *previous_ctx = nullptr;
    for (; context != nullptr; context = context->next) {
        if (ctx != context) {
            previous_ctx = context;
            continue;
        }

        if (previous_ctx != nullptr) {
            previous_ctx->next = context->next;
            break;
        }
        
        contextList = context->next;
        break;
    }

    delete ctx;
};

void AdhocState::deleteAllMatchingContext(EmuEnvState &emuenv) {
    ZoneScopedC(0xF6C2FF);
    SceNetAdhocMatchingContext *context = contextList;
    while (context != nullptr) {
        auto *next_ctx = context->next;

        delete context->rxbuf;
        context->rxbuf = nullptr;

        delete context;
        context = next_ctx;
    }
    contextList = nullptr;
};