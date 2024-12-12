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
#include <net/types.h>
#include <util/types.h>

void SceNetAdhocMatchingTarget::setSendDataStatus(SceNetAdhocMatchingSendDataStatus status) {
    if (this->sendDataStatus == status)
        return;

    if (this->sendDataStatus == SCE_NET_ADHOC_MATCHING_CONTEXT_SEND_DATA_STATUS_BUSY && status == SCE_NET_ADHOC_MATCHING_CONTEXT_SEND_DATA_STATUS_READY) {
        delete this->sendData;
        this->sendData = nullptr;
    }
    this->sendDataStatus = status;
}

void SceNetAdhocMatchingTarget::setStatus(SceNetAdhocMatchingTargetStatus status) {
    if (this->status == status) {
        return;
    }

    bool is_target_in_progress = this->status == SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES || this->status == SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES2;
    bool is_status_not_in_progress = status != SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES2 && status != SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES2;

    if (is_target_in_progress && is_status_not_in_progress) {
        deleteOptMessage();
    }

    if (this->status != SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED && status == SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED) {
        this->sendDataCount = 0;
        this->recvDataCount = 0;
    }

    if (this->status == SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED && status != SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED && this->sendDataStatus != SCE_NET_ADHOC_MATCHING_CONTEXT_SEND_DATA_STATUS_READY) {
        if (this->sendDataStatus == SCE_NET_ADHOC_MATCHING_CONTEXT_SEND_DATA_STATUS_BUSY) {
            delete this->sendData;
            this->sendData = nullptr;
        }
        this->sendDataStatus = SCE_NET_ADHOC_MATCHING_CONTEXT_SEND_DATA_STATUS_READY;
    }

    this->status = status;
}

int SceNetAdhocMatchingTarget::setOptMessage(SceSize optLen, char *opt) {
    if (this->optLength > 0)
        deleteOptMessage();
    if (optLen == 0)
        return SCE_NET_ADHOC_MATCHING_OK;

    this->opt = new char[optLen];
    if (this->opt == nullptr)
        return SCE_NET_ADHOC_MATCHING_ERROR_NO_SPACE;

    memcpy(this->opt, opt, optLen);
    this->optLength = optLen;
    return SCE_NET_ADHOC_MATCHING_OK;
}

SceSize SceNetAdhocMatchingTarget::getOptLen() const {
    return this->optLength;
}

char *SceNetAdhocMatchingTarget::getOpt() const {
    return this->opt;
}

void SceNetAdhocMatchingTarget::deleteOptMessage() {
    if (this->opt == nullptr)
        return;

    delete this->opt;
    this->optLength = 0;
    this->opt = nullptr;
}

int SceNetAdhocMatchingTarget::setRawPacket(SceSize rawPacketLen, SceSize packetLen, char *packet) {
    if (this->packetLength > 0)
        deleteRawPacket();
    if (rawPacketLen == 0)
        return SCE_NET_ADHOC_MATCHING_OK;

    this->packet = new char[rawPacketLen];
    if (this->packet == nullptr)
        return SCE_NET_ADHOC_MATCHING_ERROR_NO_SPACE;

    memcpy(this->packet, packet, rawPacketLen);
    this->packetLength = packetLen + sizeof(SceNetAdhocMatchingMessageHeader);
    this->rawPacketLength = rawPacketLen;
    return SCE_NET_ADHOC_MATCHING_OK;
}

SceSize SceNetAdhocMatchingTarget::getPacketLen() const {
    return this->packetLength;
}

SceSize SceNetAdhocMatchingTarget::getRawPacketLen() const {
    return this->rawPacketLength;
}

char *SceNetAdhocMatchingTarget::getRawPacket() const {
    return this->packet;
}

SceNetAdhocMatchingMessageHeader SceNetAdhocMatchingTarget::getPacketHeader() const {
    if (this->packet == nullptr)
        return {};

    SceNetAdhocMatchingMessageHeader header;
    header.parse(this->packet, this->rawPacketLength);

    return header;
}

void SceNetAdhocMatchingTarget::deleteRawPacket() {
    if (this->packet == nullptr)
        return;

    delete this->packet;
    this->packetLength = 0;
    this->rawPacketLength = 0;
    this->packet = nullptr;
}