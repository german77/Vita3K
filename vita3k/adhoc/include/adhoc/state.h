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

#pragma once

#include "util/types.h"
#include <condition_variable>
#include <module/module.h>

#include <kernel/callback.h>
#include <mem/util.h>
#include <mutex>
#include <vector>
#include <net/state.h>

#define SCE_NET_ADHOC_DEFAULT_PORT 0xe4a
#define SCE_NET_ADHOC_MATCHING_MAXNUM 16
#define SCE_NET_ADHOC_MATCHING_MAXOPTLEN 9196
#define SCE_NET_ADHOC_MATCHING_MAXDATALEN 9204
#define SCE_NET_ADHOC_MATCHING_MAXHELLOOPTLEN 1426

DECL_EXPORT(SceInt32, sceNetCtlAdhocGetInAddr, SceNetInAddr *inaddr);

int sendHelloReqToPipe(void *arg);
int adhocMatchingEventThread(EmuEnvState &emuenv, int id);
int adhocMatchingInputThread(EmuEnvState &emuenv, int id);
int adhocMatchingCalloutThread(EmuEnvState &emuenv, int id);

int pipe88CallbackType2(void *args);
int pipe88CallbackType3(void *args);
int pipeA0Callback(void *args);
int pipeHelloCallback(void *args);

struct SceNetAdhocHandlerArguments {
    uint32_t id;
    uint32_t event;
    Address peer;
    uint32_t optlen;
    Address opt;
};

static_assert(sizeof(SceNetAdhocHandlerArguments) == 20);

enum SceNetAdhocMatchingErrorCode {
    SCE_NET_ADHOC_MATCHING_OK = 0x0,
    SCE_NET_ADHOC_MATCHING_ERROR_INVALID_MODE = 0x80413101,
    SCE_NET_ADHOC_MATCHING_ERROR_INVALID_PORT = 0x80413102,
    SCE_NET_ADHOC_MATCHING_ERROR_INVALID_MAXNUM = 0x80413103,
    SCE_NET_ADHOC_MATCHING_ERROR_RXBUF_TOO_SHORT = 0x80413104,
    SCE_NET_ADHOC_MATCHING_ERROR_INVALID_OPTLEN = 0x80413105,
    SCE_NET_ADHOC_MATCHING_ERROR_INVALID_ARG = 0x80413106,
    SCE_NET_ADHOC_MATCHING_ERROR_INVALID_ID = 0x80413107,
    SCE_NET_ADHOC_MATCHING_ERROR_ID_NOT_AVAIL = 0x80413108,
    SCE_NET_ADHOC_MATCHING_ERROR_NO_SPACE = 0x80413109,
    SCE_NET_ADHOC_MATCHING_ERROR_IS_RUNNING = 0x8041310a,
    SCE_NET_ADHOC_MATCHING_ERROR_NOT_RUNNING = 0x8041310b,
    SCE_NET_ADHOC_MATCHING_ERROR_UNKNOWN_TARGET = 0x8041310c,
    SCE_NET_ADHOC_MATCHING_ERROR_TARGET_NOT_READY = 0x8041310d,
    SCE_NET_ADHOC_MATCHING_ERROR_EXCEED_MAXNUM = 0x8041310e,
    SCE_NET_ADHOC_MATCHING_ERROR_REQUEST_IN_PROGRESS = 0x8041310f,
    SCE_NET_ADHOC_MATCHING_ERROR_ALREADY_ESTABLISHED = 0x80413110,
    SCE_NET_ADHOC_MATCHING_ERROR_BUSY = 0x80413111,
    SCE_NET_ADHOC_MATCHING_ERROR_ALREADY_INITIALIZED = 0x80413112,
    SCE_NET_ADHOC_MATCHING_ERROR_NOT_INITIALIZED = 0x80413113,
    SCE_NET_ADHOC_MATCHING_ERROR_PORT_IN_USE = 0x80413114,
    SCE_NET_ADHOC_MATCHING_ERROR_STACKSIZE_TOO_SHORT = 0x80413115,
    SCE_NET_ADHOC_MATCHING_ERROR_INVALID_DATALEN = 0x80413116,
    SCE_NET_ADHOC_MATCHING_ERROR_NOT_ESTABLISHED = 0x80413117,
    SCE_NET_ADHOC_MATCHING_ERROR_DATA_BUSY = 0x80413118,
    SCE_NET_ADHOC_MATCHING_ERROR_INVALID_ALIGNMENT = 0x80413119
};

enum SceNetCalloutErrorCode {
    SCE_NET_CALLOUT_OK = 0x0,
    SCE_NET_CALLOUT_ERROR_NOT_INITIALIZED = 0x80558001,
    SCE_NET_CALLOUT_ERROR_NOT_TERMINATED = 0x80558002,
    SCE_NET_CALLOUT_ERROR_DUPLICATED = 0x80558006,
};

enum SceNetAdhocMatchingMode : uint8_t {
    SCE_NET_ADHOC_MATCHING_MODE_PARENT = 1,
    SCE_NET_ADHOC_MATCHING_MODE_CHILD,
    SCE_NET_ADHOC_MATCHING_MODE_UDP,
    SCE_NET_ADHOC_MATCHING_MODE_MAX,
};

enum SceNetAdhocMatchingHandlerEventType {
    SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_HELLO = 1,
    SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_REQUEST = 2,
    SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_LEAVE = 3,
    SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_DENY = 4,
    SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_CANCEL = 5,
    SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_ACCEPT = 6,
    SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_ESTABLISHED = 7,
    SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_TIMEOUT = 8,
    SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_ERROR = 9,
    SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_BYE = 10,
    SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_DATA = 11,
    SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_DATA_ACK = 12,
    SCE_NET_ADHOC_MATCHING_HANDLER_EVENT_DATA_TIMEOUT = 13
};

enum SceNetAdhocMatchingPacketType : uint8_t {
    SCE_NET_ADHOC_MATCHING_PACKET_TYPE_HELLO = 1,
    SCE_NET_ADHOC_MATCHING_PACKET_TYPE_HELLO_ACK = 2,
    SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK3 = 3,
    SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK4 = 4,
    SCE_NET_ADHOC_MATCHING_PACKET_TYPE_CANCEL = 5,
    SCE_NET_ADHOC_MATCHING_PACKET_TYPE_ADDRS = 6,
    SCE_NET_ADHOC_MATCHING_PACKET_TYPE_ADDRS_ACK = 7,
    SCE_NET_ADHOC_MATCHING_PACKET_TYPE_BYE = 8,
    SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK9 = 9,
    SCE_NET_ADHOC_MATCHING_PACKET_TYPE_DATA = 10,
    SCE_NET_ADHOC_MATCHING_PACKET_TYPE_DATA_ACK = 11
};

enum SceNetAdhocMatchingEvent : uint32_t {
    SCE_NET_ADHOC_MATCHING_EVENT_ABORT = 0,
    SCE_NET_ADHOC_MATCHING_EVENT_PACKET = 1,
    SCE_NET_ADHOC_MATCHING_EVENT_UNK2 = 2,
    SCE_NET_ADHOC_MATCHING_EVENT_UNK3 = 3,
    SCE_NET_ADHOC_MATCHING_EVENT_HELLO_SEND = 4,
    SCE_NET_ADHOC_MATCHING_EVENT_UNK5 = 5,
};

enum SceNetAdhocMatchingTargetStatus : uint32_t {
    SCE_NET_ADHOC_MATCHING_TARGET_STATUS_CANCELLED = 1,
    SCE_NET_ADHOC_MATCHING_TARGET_STATUS_2 = 2,
    SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES = 3,
    SCE_NET_ADHOC_MATCHING_TARGET_STATUS_INPROGRES2 = 4,
    SCE_NET_ADHOC_MATCHING_TARGET_STATUS_ESTABLISHED = 5,
};

enum SceNetAdhocMatchingContextStatus {
    SCE_NET_ADHOC_MATCHING_CONTEXT_STATUS_NOT_RUNNING = 0,
    SCE_NET_ADHOC_MATCHING_CONTEXT_STATUS_STOPPING = 1,
    SCE_NET_ADHOC_MATCHING_CONTEXT_STATUS_RUNNING = 2,
};

struct SceNetAdhocMatchingHandler {
    Address pc;
};

struct SceNetAdhocMatchingDataMessage {
    uint8_t one;
    SceNetAdhocMatchingPacketType type;
    SceUShort16 packetLength;
    int targetCount;
    int other;
    char data[0x100];
};

struct SceNetAdhocMatchingOptMessage {
    uint8_t one;
    SceNetAdhocMatchingPacketType type;
    SceUShort16 packetLength;
    std::vector<char> dataBuffer;
    int targetCount;
    char zero[0xc];

    std::vector<char> serialize() {
        std::vector<char> data(dataBuffer.size() + 0x14);
        memcpy(data.data(), &one, sizeof(uint8_t));
        memcpy(data.data() + 0x1, &type, sizeof(SceNetAdhocMatchingPacketType));
        memcpy(data.data() + 0x2, &packetLength, sizeof(SceUShort16));
        memcpy(data.data() + 0x4, dataBuffer.data(), dataBuffer.size());
        memcpy(data.data() + 0x4 + dataBuffer.size(), &targetCount, sizeof(int));
        memcpy(data.data() + 0x8 + dataBuffer.size(), &zero, sizeof(0xc));
        return data;
    }

    void parse(char *data, SceSize dataLen) {
        memcpy(&one, data, sizeof(uint8_t));
        memcpy(&type, data + 0x1, sizeof(SceNetAdhocMatchingPacketType));
        memcpy(&packetLength, data + 0x2, sizeof(SceUShort16));
        dataBuffer.resize(dataLen - 0x14);
        memcpy(dataBuffer.data(), data + 0x4, dataBuffer.size());
        memcpy(&targetCount, data + 0x4 + dataBuffer.size(), sizeof(int));
        memcpy(&zero, data + 0x8 + dataBuffer.size(), sizeof(0xc));
    }
};

struct SceNetAdhocMatchingByeMessage {
    uint8_t one;
    SceNetAdhocMatchingPacketType type;
    SceUShort16 packetLength;
};

struct SceNetAdhocMatchingHelloMessage {
    uint8_t one; //! ALWAYS 1
    SceNetAdhocMatchingPacketType type;
    SceUShort16 packetLength;
    int helloInterval;
    int rexmtInterval;
    std::vector<char> optBuffer;
    int unk_6c;
    char zero[0xc];

    std::vector<char> serialize() {
        std::vector<char> data(optBuffer.size() + 0x1c);
        memcpy(data.data(), &one, sizeof(uint8_t));
        memcpy(data.data() + 0x1, &type, sizeof(SceNetAdhocMatchingPacketType));
        memcpy(data.data() + 0x2, &packetLength, sizeof(SceUShort16));
        memcpy(data.data() + 0x4, &helloInterval, sizeof(int));
        memcpy(data.data() + 0x8, &rexmtInterval, sizeof(int));
        memcpy(data.data() + 0xC, optBuffer.data(), optBuffer.size());
        memcpy(data.data() + 0xC + optBuffer.size(), &unk_6c, sizeof(int));
        memcpy(data.data() + 0x10 + optBuffer.size(), &zero, sizeof(0xc));
        return data;
    }

    void parse(char *data, SceSize dataLen) {
        memcpy(&one, data, sizeof(uint8_t));
        memcpy(&type, data + 0x1, sizeof(SceNetAdhocMatchingPacketType));
        memcpy(&packetLength, data + 0x2, sizeof(SceUShort16));
        memcpy(&helloInterval, data + 0x4, sizeof(int));
        memcpy(&rexmtInterval, data + 0x8, sizeof(int));
        optBuffer.resize(dataLen - 0x1c);
        memcpy(optBuffer.data(), data + 0xC, optBuffer.size());
        memcpy(&unk_6c, data + 0xC + optBuffer.size(), sizeof(int));
        memcpy(&zero, data + 0x10 + optBuffer.size(), sizeof(0xc));
    }
};

struct SceNetAdhocMatchingMemberMessage {
    uint8_t one;
    SceNetAdhocMatchingPacketType type;
    SceUShort16 packetLength;
    SceNetInAddr parent;
    std::vector<SceNetInAddr> members;

    std::vector<char> serialize() {
        std::vector<char> data(members.size() * sizeof(SceNetInAddr) + 0x8);
        memcpy(data.data(), &one, sizeof(uint8_t));
        memcpy(data.data() + 0x1, &type, sizeof(SceNetAdhocMatchingPacketType));
        memcpy(data.data() + 0x2, &packetLength, sizeof(SceUShort16));
        memcpy(data.data() + 0x4, &parent, sizeof(SceNetInAddr));
        memcpy(data.data() + 0x8, members.data(), members.size() * sizeof(SceNetInAddr));
        return data;
    }

    void parse(char *data, SceSize dataLen) {
        memcpy(&one, data, sizeof(uint8_t));
        memcpy(&type, data + 0x1, sizeof(SceNetAdhocMatchingPacketType));
        memcpy(&packetLength, data + 0x2, sizeof(SceUShort16));
        memcpy(&parent, data + 0x4, sizeof(SceNetInAddr));
        members.resize((dataLen - 0x8)/sizeof(SceNetInAddr));
        memcpy(members.data(), data + 0x8, members.size() * sizeof(SceNetInAddr));
    }
};

struct SceNetAdhocMatchingTarget;

struct SceNetAdhocMatchingPipeMessage {
    SceNetAdhocMatchingEvent type;
    SceNetAdhocMatchingTarget *peer;
    int flags;
};

struct SceNetAdhocMatchingMember {
    SceNetInAddr addr;
};

struct SceNetAdhocMatchingCalloutFunction {
    SceNetAdhocMatchingCalloutFunction *next;
    uint64_t execAt;
    int (*function)(void *);
    void *args;
};

struct SceNetAdhocMatchingTarget {
    SceNetAdhocMatchingTarget *next;
    SceNetAdhocMatchingTargetStatus status;
    SceNetInAddr addr;
    int unk_0c;
    SceSize keepAliveInterval;

    SceSize rawPacketLength;
    char *rawPacket;
    int packetLength;

    SceSize optLength;
    char *opt;

    SceNetAdhocMatchingPipeMessage pipeMsg28;
    SceNetAdhocMatchingPipeMessage pipeMsg88;

    bool is_88_pending;
    int retryCount2;

    int retryCount;
    int msgPipeUid[2]; // 0 = read, 1 = write

    int unk_50;
    bool delete_target;
    int targetCount;
    int unk_5c;

    SceSize sendDataCount;
    int unk_64;
    int sendDataStatus;
    SceSize sendDataLength;
    char *sendData;

    SceNetAdhocMatchingPipeMessage msgA0;
    bool is_a0_pending;
    int context_uuid;

    SceNetAdhocMatchingCalloutFunction timedFunction88;
    SceNetAdhocMatchingCalloutFunction timedFunctionA0;
};

struct SceNetAdhocMatchingCalloutSyncing {
    int initializeCalloutThread(EmuEnvState &emuenv, SceUID thread_id, int id, int threadPriority, int threadStackSize, int threadCpuAffinityMask);
    void closeCalloutThread();

    int addTimedFunction(SceNetAdhocMatchingCalloutFunction *calloutFunction, SceLong64 interval, int (*function)(void *), void *args);
    int deleteTimedFunction(SceNetAdhocMatchingCalloutFunction *calloutFunction, bool *is_deleted);

    std::thread calloutThread;
    std::mutex mutex;
    std::condition_variable condvar;
    bool isInitialized;
    bool shouldExit;
    SceNetAdhocMatchingCalloutFunction* functionList;
};

struct SceNetAdhocMatchingContext {    
    int initializeInputThread(EmuEnvState &emuenv, SceUID thread_id, int threadPriority, int threadStackSize, int threadCpuAffinityMask);
    void closeInputThread(EmuEnvState &emuenv, SceUID thread_id);

    int initializeEventHandler(EmuEnvState &emuenv, SceUID thread_id, int threadPriority, int threadStackSize, int threadCpuAffinityMask);
    void closeEventHandler();

    int initializeSendSocket(EmuEnvState &emuenv, SceUID thread_id);
    void closeSendSocket(EmuEnvState &emuenv, SceUID thread_id);

    void processPacketFromTarget(EmuEnvState &emuenv, SceUID thread_id, SceNetAdhocMatchingTarget *peer);

    // Target
    void setTargetSendDataStatus(SceNetAdhocMatchingTarget *target, int status);
    void setTargetStatus(SceNetAdhocMatchingTarget *target, SceNetAdhocMatchingTargetStatus status);
    SceNetAdhocMatchingTarget *newTarget(uint32_t addr);
    SceNetAdhocMatchingTarget *findTargetByAddr(uint32_t addr);
    void getTargetAddrList(SceNetAdhocMatchingTargetStatus status, SceNetInAddr *addrList, SceSize &addrListSize);
    SceSize countTargetsWithStatusOrBetter(SceNetAdhocMatchingTargetStatus status);
    bool isTargetAddressHigher(SceNetAdhocMatchingTarget *target);
    void deleteTarget(SceNetAdhocMatchingTarget *target);
    void deleteAllTargets(EmuEnvState &emuenv, SceUID thread_id);

    // Member list message
    int createMembersList();
    int getMembers(SceSize *membersNum, SceNetAdhocMatchingMember *members);
    int sendMemberListToTarget(SceNetAdhocMatchingTarget *target);
    int processMemberListPacket(char *packet, SceSize packetLength);
    void clearMemberList();

    // Hello optional data
    int getHelloOpt(SceSize *oOptlen, void *oOpt);
    int setHelloOpt(SceSize optlen, void *opt);
    int broadcastHello(EmuEnvState &emuenv, SceUID thread_id);
    void resetHelloOpt();

    void addHelloTimedFunct(EmuEnvState &emuenv, uint64_t time_interval);
    void addA0TimedFunction(EmuEnvState &emuenv, SceNetAdhocMatchingTarget *target);
    void add88TimedFunct(EmuEnvState &emuenv, SceNetAdhocMatchingTarget *target);
    void add88TimedFunctionWithParentInterval(EmuEnvState &emuenv, SceNetAdhocMatchingTarget *target);
    void deleteHelloTimedFunction(EmuEnvState &emuenv);
    void deleteA0TimedFunction(EmuEnvState &emuenv, SceNetAdhocMatchingTarget *target);
    void deleteAllTimedFunctions(EmuEnvState &emuenv, SceNetAdhocMatchingTarget *target);

    void notifyHandler(EmuEnvState &emuenv, SceUID thread_id, SceNetAdhocMatchingHandlerEventType type, SceNetInAddr *peer, SceSize optLen = 0, void *opt = nullptr);

    int sendDataMessageToTarget(EmuEnvState &emuenv, SceUID thread_id, SceNetAdhocMatchingTarget *target, SceNetAdhocMatchingPacketType type, int datalen, char *data);
    int sendOptDataToTarget(EmuEnvState &emuenv, SceUID thread_id, SceNetAdhocMatchingTarget *target, SceNetAdhocMatchingPacketType type, int optlen, char *opt);
        
    int broadcastBye(EmuEnvState &emuenv, SceUID thread_id);


    SceNetAdhocMatchingContext *next = nullptr;
    unsigned int id;
    SceNetAdhocMatchingContextStatus status = SCE_NET_ADHOC_MATCHING_CONTEXT_STATUS_NOT_RUNNING;
    SceNetAdhocMatchingMode mode;
    int maxnum;
    SceUShort16 port;

    int rxbuflen;
    char *rxbuf;

    unsigned int helloInterval;
    unsigned int keepAliveInterval;
    unsigned int retryCount;
    unsigned int rexmtInterval;

    SceNetAdhocMatchingHandler handler;

    int sendSocket;
    int recvSocket; // Socket used by parent to broadcast

    std::thread eventThread;
    std::thread inputThread;
    SceUID event_thread_id;
    SceUID input_thread_id;

    int msgPipeUid[2]; // 0 = read, 1 = write

    unsigned int totalHelloLength;
    SceNetAdhocMatchingHelloMessage *helloMsg;
    SceNetAdhocMatchingPipeMessage helloPipeMsg;

    SceSize memberMsgSize;
    SceNetAdhocMatchingMemberMessage *memberMsg;

    bool shouldHelloReqBeProcessed;
    int helloOptionFlag;

    SceNetAdhocMatchingTarget *targetList;

    SceNetAdhocMatchingCalloutSyncing calloutSyncing;
    SceNetAdhocMatchingCalloutFunction helloTimedFunction;

    uint32_t ownAddress;
    uint16_t ownPort;
};

class AdhocState {
public:
    int initializeMutex();
    int deleteMutex();
    std::recursive_mutex &getMutex();

    int createMSpace(SceSize poolsize, void *poolptr);
    int deleteMSpace();

    int initializeMatchingContextList();
    int isAnyMatchingContextRunning();
    SceNetAdhocMatchingContext *findMatchingContextById(int id);
    int createMatchingContext(SceUShort16 port);
    void deleteMatchingContext(SceNetAdhocMatchingContext *ctx);
    void deleteAllMatchingContext();

public: // Globals
    bool is_initialized = false;
    SceNetInAddr addr;
    SceUID next_uid = 0;

private:
    bool is_mutex_initialized = false;

    std::recursive_mutex mutex;
    SceNetAdhocMatchingContext *contextList = NULL;
    SceUID matchingCtxCount = 1;
};
