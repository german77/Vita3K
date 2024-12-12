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
int adhocMatchingEventThread(EmuEnvState &emuenv, SceUID thread_id, SceUID id);
int adhocMatchingInputThread(EmuEnvState &emuenv, SceUID thread_id, SceUID id);
int adhocMatchingCalloutThread(EmuEnvState &emuenv, SceUID id);

int registerTargetTimeoutCallback(void *args);
int targetTimeoutCallback(void *args);
int sendDataTimeoutCallback(void *args);
int pipeHelloCallback(void *args);

typedef Address SceNetAdhocMatchingHandler;

struct SceNetAdhocHandlerArguments {
    uint32_t id;
    uint32_t event;
    Address peer;
    uint32_t optlen;
    Address opt;
};
static_assert(sizeof(SceNetAdhocHandlerArguments) == 0x14, "SceNetAdhocHandlerArguments is an invalid size");

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
    SCE_NET_ADHOC_MATCHING_MODE_P2P,
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
    SCE_NET_ADHOC_MATCHING_PACKET_TYPE_MEMBER_LIST = 6,
    SCE_NET_ADHOC_MATCHING_PACKET_TYPE_MEMBER_LIST_ACK = 7,
    SCE_NET_ADHOC_MATCHING_PACKET_TYPE_BYE = 8,
    SCE_NET_ADHOC_MATCHING_PACKET_TYPE_UNK9 = 9,
    SCE_NET_ADHOC_MATCHING_PACKET_TYPE_DATA = 10,
    SCE_NET_ADHOC_MATCHING_PACKET_TYPE_DATA_ACK = 11
};

enum SceNetAdhocMatchingEvent : uint32_t {
    SCE_NET_ADHOC_MATCHING_EVENT_ABORT = 0,
    SCE_NET_ADHOC_MATCHING_EVENT_PACKET = 1,
    SCE_NET_ADHOC_MATCHING_EVENT_REGISTRATION_TIMEOUT = 2,
    SCE_NET_ADHOC_MATCHING_EVENT_TARGET_TIMEOUT = 3,
    SCE_NET_ADHOC_MATCHING_EVENT_HELLO_TIMEOUT = 4,
    SCE_NET_ADHOC_MATCHING_EVENT_DATA_TIMEOUT = 5,
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

enum SceNetAdhocMatchingSendDataStatus {
    SCE_NET_ADHOC_MATCHING_CONTEXT_SEND_DATA_STATUS_READY = 1,
    SCE_NET_ADHOC_MATCHING_CONTEXT_SEND_DATA_STATUS_BUSY = 2,
};

struct SceNetAdhocMatchingMessageHeader {
    uint8_t one; //! ALWAYS 1
    SceNetAdhocMatchingPacketType type;
    SceUShort16 packetLength;
};
static_assert(sizeof(SceNetAdhocMatchingMessageHeader) == 0x4, "SceNetAdhocMatchingMessageHeader is an invalid size");

struct SceNetAdhocMatchingDataMessage {
    SceNetAdhocMatchingMessageHeader header;
    int targetCount;
    int other;
    std::vector<char> dataBuffer;

    std::size_t messageSize() const {
        return sizeof(header) + dataBuffer.size() + 0x8;
    }

    std::vector<char> serialize() const{
        std::vector<char> data(messageSize());
        memcpy(data.data(), &header, sizeof(SceNetAdhocMatchingMessageHeader));
        memcpy(data.data() + sizeof(header), &targetCount, sizeof(int));
        memcpy(data.data() + sizeof(header) + 0x4, &other, sizeof(int));
        memcpy(data.data() + sizeof(header) + 0x8, dataBuffer.data(), dataBuffer.size());
        return data;
    }

    void parse(char *data, SceSize dataLen) {
        assert(dataLen >= sizeof(header) + 0x8);
        dataBuffer.resize(dataLen - sizeof(header) - 0x8);

        memcpy(&header, data, sizeof(SceNetAdhocMatchingMessageHeader));
        memcpy(dataBuffer.data(), data + sizeof(header), dataBuffer.size());
        memcpy(&targetCount, data + sizeof(header), sizeof(int));
        memcpy(&other, data + sizeof(header) + 0x4, sizeof(int));
        memcpy(dataBuffer.data(), data + sizeof(header) + 0x8, dataBuffer.size());
    }
};

struct SceNetAdhocMatchingOptMessage {
    SceNetAdhocMatchingMessageHeader header;
    std::vector<char> dataBuffer;
    int targetCount;
    char zero[0xc];

    std::size_t messageSize() const{
        return sizeof(header) + dataBuffer.size() + 0x10;
    }

    std::vector<char> serialize() const{
        std::vector<char> data(messageSize());
        memcpy(data.data(), &header, sizeof(SceNetAdhocMatchingMessageHeader));
        memcpy(data.data() + sizeof(header), dataBuffer.data(), dataBuffer.size());
        memcpy(data.data() + sizeof(header) + dataBuffer.size(), &targetCount, sizeof(int));
        memcpy(data.data() + sizeof(header) + dataBuffer.size() + 0x4, &zero, sizeof(0xc));
        return data;
    }

    void parse(char *data, SceSize dataLen) {
        assert(dataLen >= sizeof(header));
        dataBuffer.resize(dataLen - sizeof(header) - 0x10);

        memcpy(&header, data, sizeof(SceNetAdhocMatchingMessageHeader));
        memcpy(dataBuffer.data(), data + sizeof(header), dataBuffer.size());
        memcpy(&targetCount, data + sizeof(header) + dataBuffer.size(), sizeof(int));
        memcpy(&zero, data + sizeof(header) + dataBuffer.size() + 0x4, sizeof(0xc));
    }
};


struct SceNetAdhocMatchingHelloMessage {
    SceNetAdhocMatchingMessageHeader header;
    int helloInterval;
    int rexmtInterval;
    std::vector<char> optBuffer;
    int unk_6c;
    char zero[0xc];

    std::size_t messageSize() const {
        return sizeof(header) + optBuffer.size() + 0x18;
    }

    std::vector<char> serialize() const{
        std::vector<char> data(messageSize());
        memcpy(data.data(), &header, sizeof(SceNetAdhocMatchingMessageHeader));
        memcpy(data.data() + sizeof(header), &helloInterval, sizeof(int));
        memcpy(data.data() + sizeof(header) + 0x4, &rexmtInterval, sizeof(int));
        memcpy(data.data() + sizeof(header) + 0x8, optBuffer.data(), optBuffer.size());
        memcpy(data.data() + sizeof(header) + 0x8 + optBuffer.size(), &unk_6c, sizeof(int));
        memcpy(data.data() + sizeof(header) + 0xC + optBuffer.size(), &zero, sizeof(0xc));
        return data;
    }

    void parse(char *data, SceSize dataLen) {
        assert(dataLen >= sizeof(header) + 0x8);
        optBuffer.resize(dataLen - sizeof(header) - 0x18);

        memcpy(&header, data, sizeof(SceNetAdhocMatchingMessageHeader));
        memcpy(&helloInterval, data + sizeof(header), sizeof(int));
        memcpy(&rexmtInterval, data + sizeof(header) + 0x4, sizeof(int));
        memcpy(optBuffer.data(), data + sizeof(header) + 0x8, optBuffer.size());
        memcpy(&unk_6c, data + sizeof(header) + 0x8 + optBuffer.size(), sizeof(int));
        memcpy(&zero, data + sizeof(header) + 0xC + optBuffer.size(), sizeof(0xc));
    }
};

struct SceNetAdhocMatchingMemberMessage {
    SceNetAdhocMatchingMessageHeader header;
    SceNetInAddr parent;
    std::vector<SceNetInAddr> members;

    std::size_t messageSize() const {
        const std::size_t memberSize = members.size() * sizeof(SceNetInAddr);
        return sizeof(header) + sizeof(parent) + memberSize;
    }

    std::vector<char> serialize() const {
        std::vector<char> data(messageSize());
        memcpy(data.data(), &header, sizeof(SceNetAdhocMatchingMessageHeader));
        memcpy(data.data() + sizeof(header), &parent, sizeof(SceNetInAddr));
        memcpy(data.data() + sizeof(header) + sizeof(parent), &members, members.size() * sizeof(SceNetInAddr));
        return data;
    }

    void parse(char *data, SceSize dataLen) {
        assert(dataLen >= sizeof(header) + sizeof(parent));
        assert(dataLen % 4 == 0);

        const std::size_t entries = (dataLen / sizeof(SceNetInAddr)) - 2;
        members.resize(entries);
        memcpy(&header, data, sizeof(SceNetAdhocMatchingMessageHeader));
        memcpy(&parent, data + sizeof(header), sizeof(SceNetInAddr));
        memcpy(members.data(), data + sizeof(header) + sizeof(parent), entries * sizeof(SceNetInAddr));
    }
};

struct SceNetAdhocMatchingTarget;

struct SceNetAdhocMatchingPipeMessage {
    SceNetAdhocMatchingEvent type;
    SceNetAdhocMatchingTarget *target;
    bool isSheduled;
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

struct SceNetAdhocMatchingAckTimeout {
    SceNetAdhocMatchingPipeMessage message;
    bool isAckPending;
    int retryCount;
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
    int retryCount;
    int msgPipeUid[2]; // 0 = read, 1 = write

    int unk_50;
    bool delete_target;
    int targetCount;
    int unk_5c;

    SceSize sendDataCount;
    int unk_64;
    SceNetAdhocMatchingSendDataStatus sendDataStatus;
    char *sendData;

    SceNetAdhocMatchingAckTimeout targetTimeout;
    SceNetAdhocMatchingAckTimeout sendDataTimeout;

    SceNetAdhocMatchingCalloutFunction targetTimeoutFunction;
    SceNetAdhocMatchingCalloutFunction sendDataTimeoutFunction;

    void setStatus(SceNetAdhocMatchingTargetStatus status);
    void setSendDataStatus(SceNetAdhocMatchingSendDataStatus status);
    void deleteRawPacket();
    void deleteOptMessage();
};

struct SceNetAdhocMatchingCalloutSyncing {
    int initializeCalloutThread(EmuEnvState &emuenv, SceUID thread_id, SceUID id, int threadPriority, int threadStackSize, int threadCpuAffinityMask);
    void closeCalloutThread();

    int addTimedFunction(SceNetAdhocMatchingCalloutFunction *calloutFunction, SceLong64 interval, int (*function)(void *), void *args);
    int deleteTimedFunction(SceNetAdhocMatchingCalloutFunction *calloutFunction, bool *is_deleted = nullptr);

    std::thread calloutThread;
    std::mutex mutex;
    std::condition_variable condvar;
    bool isInitialized;
    bool shouldExit;
    SceNetAdhocMatchingCalloutFunction* functionList;
};

class SceNetAdhocMatchingContext {
public:
    int initialize(SceNetAdhocMatchingMode mode, int maxnum, SceUShort16 port, int rxbuflen, unsigned int helloInterval, unsigned int keepaliveInterval, int retryCount, unsigned int rexmtInterval, Ptr<void> handlerAddr);
    void finalize();

    int start(EmuEnvState &emuenv, SceUID thread_id, int threadPriority, int threadStackSize, int threadCpuAffinityMask, SceSize helloOptlen, char *helloOpt);
    int stop(EmuEnvState &emuenv, SceUID thread_id);

    int initializeInputThread(EmuEnvState &emuenv, SceUID thread_id, int threadPriority, int threadStackSize, int threadCpuAffinityMask);
    void closeInputThread(EmuEnvState &emuenv, SceUID thread_id);

    int initializeEventHandler(EmuEnvState &emuenv, SceUID thread_id, int threadPriority, int threadStackSize, int threadCpuAffinityMask);
    void closeEventHandler();

    int initializeSendSocket(EmuEnvState &emuenv, SceUID thread_id);
    void closeSendSocket(EmuEnvState &emuenv, SceUID thread_id);

    SceNetAdhocMatchingContext *getNext();
    void setNext(SceNetAdhocMatchingContext* next_context);

    SceUID getId() const;
    void setId(SceUID id);

    SceUShort16 getPort() const;
    SceNetAdhocMatchingContextStatus getStatus() const;
    SceNetAdhocMatchingMode getMode() const;
    SceNetAdhocMatchingCalloutSyncing &getCalloutSyncing();
    SceNetAdhocMatchingTarget *findTargetByAddr(SceNetInAddr *addr) const;
    int getReadPipeUid() const;
    int getWritePipeUid() const;

    int getMembers(SceSize &outMembersNum, SceNetAdhocMatchingMember *outMembers) const;
    int getHelloOpt(SceSize &outOptlen, void *outOpt) const;
    int setHelloOpt(SceSize optlen, void *opt);
    void deleteTarget(SceNetAdhocMatchingTarget *target);

    void abortSendData(EmuEnvState &emuenv, SceNetAdhocMatchingTarget &target);
    int cancelTargetWithOpt(EmuEnvState &emuenv, SceUID thread_id, SceNetAdhocMatchingTarget &target, SceSize optLen, char *opt);
    int selectTarget(EmuEnvState &emuenv, SceUID thread_id, SceNetAdhocMatchingTarget &target, SceSize optlen, char *opt);
    int sendData(EmuEnvState &emuenv, SceUID thread_id, SceNetAdhocMatchingTarget &target, SceSize dateLen, char *data);

    void handleEventMessage(EmuEnvState &emuenv, SceUID thread_id, SceNetAdhocMatchingTarget *target);
    void handleEventRegistrationTimeout(EmuEnvState &emuenv, SceUID thread_id, SceNetAdhocMatchingTarget *target);
    void handleEventTargetTimeout(EmuEnvState &emuenv, SceUID thread_id, SceNetAdhocMatchingTarget *target);
    void handleEventHelloTimeout(EmuEnvState &emuenv, SceUID thread_id);
    void handleEventDataTimeout(EmuEnvState &emuenv, SceUID thread_id, SceNetAdhocMatchingTarget *target);
    void handleIncommingPackage(SceNetInAddr *addr, SceSize packetLength, SceSize bufferLength);

public:
    SceNetAdhocMatchingPipeMessage helloPipeMsg;
    bool shouldHelloReqBeProcessed;

    int sendSocket;
    int recvSocket;

    int rxbuflen;
    char *rxbuf;

    uint32_t ownAddress;
    uint16_t ownPort;

private:
    void processPacketFromTarget(EmuEnvState &emuenv, SceUID thread_id, SceNetAdhocMatchingTarget &target);

    // Target
    void setTargetStatus(SceNetAdhocMatchingTarget &target, SceNetAdhocMatchingTargetStatus status);
    SceNetAdhocMatchingTarget *newTarget(SceNetInAddr *addr);
    void getMemberList(SceNetAdhocMatchingTargetStatus status, SceNetInAddr *addrList, SceSize &addrListSize) const;
    SceSize countTargetsWithStatusOrBetter(SceNetAdhocMatchingTargetStatus status) const;
    bool isTargetAddressHigher(SceNetAdhocMatchingTarget &target) const;
    void deleteAllTargets(EmuEnvState &emuenv, SceUID thread_id);

    // Member list message
    int createMembersList();
    int sendMemberListToTarget(SceNetAdhocMatchingTarget *target);
    int processMemberListPacket(char *packet, SceSize packetLength);
    void deleteMemberList();

    // Hello optional data
    void deleteHelloMessage();


    void addHelloTimedFunct(EmuEnvState &emuenv, uint64_t time_interval);
    void addSendDataTimeout(EmuEnvState &emuenv, SceNetAdhocMatchingTarget &target);
    void addRegisterTargetTimeout(EmuEnvState &emuenv, SceNetAdhocMatchingTarget &target);
    void addTargetTimeout(EmuEnvState &emuenv, SceNetAdhocMatchingTarget &target);
    void deleteHelloTimedFunction(EmuEnvState &emuenv);
    void deleteSendDataTimeout(EmuEnvState &emuenv, SceNetAdhocMatchingTarget &target);
    void deleteAllTimedFunctions(EmuEnvState &emuenv, SceNetAdhocMatchingTarget &target);

    void notifyHandler(EmuEnvState &emuenv, SceUID thread_id, SceNetAdhocMatchingHandlerEventType type, SceNetInAddr *peer, SceSize optLen = 0, void *opt = nullptr);

    int sendDataMessageToTarget(EmuEnvState &emuenv, SceUID thread_id, SceNetAdhocMatchingTarget &target, SceNetAdhocMatchingPacketType type, int datalen = 0, char *data = nullptr);
    int sendOptDataToTarget(EmuEnvState &emuenv, SceUID thread_id, SceNetAdhocMatchingTarget &target, SceNetAdhocMatchingPacketType type, int optlen = 0, char *opt = nullptr);

    int broadcastHello(EmuEnvState &emuenv, SceUID thread_id);
    int broadcastBye(EmuEnvState &emuenv, SceUID thread_id);


    SceNetAdhocMatchingContext *next = nullptr;
    SceUID id;
    SceNetAdhocMatchingContextStatus status = SCE_NET_ADHOC_MATCHING_CONTEXT_STATUS_NOT_RUNNING;
    SceNetAdhocMatchingMode mode;
    int maxnum;
    SceUShort16 port;

    unsigned int helloInterval;
    unsigned int keepAliveInterval;
    unsigned int retryCount;
    unsigned int rexmtInterval;

    SceNetAdhocMatchingHandler handler;

    std::thread eventThread;
    std::thread inputThread;
    SceUID event_thread_id;
    SceUID input_thread_id;

    int msgPipeUid[2]; // 0 = read, 1 = write

    SceNetAdhocMatchingHelloMessage *helloMsg;
    SceNetAdhocMatchingMemberMessage *memberMsg;

    int helloOptionFlag;

    SceNetAdhocMatchingTarget *targetList;

    SceNetAdhocMatchingCalloutSyncing calloutSyncing;
    SceNetAdhocMatchingCalloutFunction helloTimedFunction;
};

class AdhocState {
public:
    int initializeMutex();
    int deleteMutex();
    std::mutex &getMutex();

    int createMSpace(SceSize poolsize, void *poolptr);
    int deleteMSpace();

    int initializeMatchingContextList();
    int isAnyMatchingContextRunning();
    SceNetAdhocMatchingContext *findMatchingContextById(SceUID id);
    int createMatchingContext(SceUShort16 port);
    void deleteMatchingContext(SceNetAdhocMatchingContext *ctx);
    void deleteAllMatchingContext();

public: // Globals
    bool is_initialized = false;
    SceNetInAddr addr;
    SceUID next_uid = 0;

private:
    bool is_mutex_initialized = false;

    std::mutex mutex;
    SceNetAdhocMatchingContext *contextList = NULL;
    SceUID matchingCtxCount = 1;
};
