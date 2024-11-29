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

int AdhocState::InitializeMutex() {
    // Initialize mutex
    is_mutex_initialized = true;
    return SCE_NET_ADHOC_MATCHING_OK;
}

int AdhocState::DeleteMutex() {
    if (is_mutex_initialized) {
        // Delete mutex
    }
    is_mutex_initialized = false;
    return SCE_NET_ADHOC_MATCHING_OK;
}

std::mutex &AdhocState::GetMutex() {
    return mutex;
}

int AdhocState::CreateMSpace(SceSize poolsize, void *poolptr) {
    // Just a placeholder. We don't really need this kind of allocation
    return SCE_NET_ADHOC_MATCHING_OK;
}

int AdhocState::DeleteMSpace() {
    // Just a placeholder. We don't really need this kind of allocation
    return SCE_NET_ADHOC_MATCHING_OK;
}

int AdhocState::InitializeMatchingContextList() {
    contextList = nullptr;
    return SCE_NET_ADHOC_MATCHING_OK;
}

int AdhocState::IsAnyMatchingContextRunning() {
    SceNetAdhocMatchingContext *context = contextList;
    for (; context != nullptr; context = context->next) {
        if (context->status != SCE_NET_ADHOC_MATCHING_CONTEXT_STATUS_NOT_RUNNING)
            return SCE_NET_ADHOC_MATCHING_ERROR_BUSY;
    }
    return SCE_NET_ADHOC_MATCHING_OK;
}

SceNetAdhocMatchingContext *AdhocState::findMatchingContextById(int id) {
    // Iterate Matching Context List
    SceNetAdhocMatchingContext *context = contextList;
    for (; context != nullptr; context = context->next) {
        if (context->id != id)
            continue;
        // Found Matching ID
        return context;
    }

    // Context not found
    return nullptr;
};

int AdhocState::createMatchingContext(SceUShort16 port) {
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

        context = findMatchingContextById(next_id);

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

void AdhocState::DestroyMatchingContext(SceNetAdhocMatchingContext *ctx) {
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

void AdhocState::DestroyAllMatchingContext() {
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