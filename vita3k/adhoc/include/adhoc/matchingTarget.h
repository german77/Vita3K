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

#pragma once

#include "adhoc/matchingContext.h"
#include "adhoc/threads.h"
#include "net/types.h"
#include "util/types.h"

enum SceNetAdhocMatchingTargetStatus : uint32_t {
    SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED = 1,
    SCE_NET_ADHOC_MATCHING_TARGET_STATUS_2 = 2,
    SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES = 3,
    SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES2 = 4,
    SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED = 5,
};

enum SceNetAdhocMatchingSendDataStatus {
    SCE_NET_ADHOC_MATCHING_CONTEXT_SEND_DATA_STATUS_READY = 1,
    SCE_NET_ADHOC_MATCHING_CONTEXT_SEND_DATA_STATUS_BUSY = 2,
};

struct SceNetAdhocMatchingAckTimeout {
    SceNetAdhocMatchingPipeMessage message;
    bool isAckPending;
    int retryCount;
};

class SceNetAdhocMatchingTarget {
public:
    void setStatus(SceNetAdhocMatchingTargetStatus status);
    void setSendDataStatus(SceNetAdhocMatchingSendDataStatus status);

    void setPipeUids(int read, int write);
    int getReadPipeUid() const;
    int getWritePipeUid() const;

    int setOptMessage(SceSize optLen, char *opt);
    SceSize getOptLen() const;
    char *getOpt() const;
    void deleteOptMessage();

    int setRawPacket(SceSize rawPacketLen, SceSize packetLen, char *packet);
    SceSize getRawPacketLen() const;
    SceSize getPacketLen() const;
    char *getRawPacket() const;
    SceNetAdhocMatchingMessageHeader getPacketHeader() const;
    void deleteRawPacket();

    SceNetAdhocMatchingTarget *next;
    SceNetAdhocMatchingTargetStatus status;
    SceNetInAddr addr;
    int unk_0c;
    SceSize keepAliveInterval;

    SceNetAdhocMatchingPipeMessage incomingPacketMessage;
    int retryCount;

    int unk_50;
    bool deleteTarget;
    int targetCount;
    SceUID uid;

    SceSize sendDataCount;
    SceSize recvDataCount;
    SceNetAdhocMatchingSendDataStatus sendDataStatus;
    char *sendData;

    SceNetAdhocMatchingAckTimeout targetTimeout;
    SceNetAdhocMatchingAckTimeout sendDataTimeout;

    SceNetAdhocMatchingCalloutFunction targetTimeoutFunction;
    SceNetAdhocMatchingCalloutFunction sendDataTimeoutFunction;

private:
    int msgPipeUid[2]; // 0 = read, 1 = write

    SceSize optLength = 0;
    char *opt = nullptr;

    SceSize packetLength = 0;
    SceSize rawPacketLength = 0;
    char *packet = nullptr;
};
