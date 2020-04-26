#ifndef MESSAGE_H_
#define MESSAGE_H_

#include <cstdint>
#include <cstddef>
#include "exception.h"
#include <arpa/inet.h>
#include "const.h"

template<typename T>
void toBE(T* pT){
    static_assert(sizeof(T) == 1u || sizeof(T) == 2u || sizeof(T) == 4u);
    switch(sizeof(T)){
        case 1u:
        break;
        case 2u:
        *pT = htons(*pT);
        break;
        case 4u:
        *pT = htonl(*pT);
        break;
    }
}

template<typename T>
void fromBE(T* pT){
    static_assert(sizeof(T) == 1u || sizeof(T) == 2u || sizeof(T) == 4u);
    switch(sizeof(T)){
        case 1u:
        break;
        case 2u:
        *pT = ntohs(*pT);
        break;
        case 4u:
        *pT = ntohl(*pT);
        break;
    }
}

#pragma pack(push, 1)

const static uint16_t MESSAGE_INVALID = 0;
const static uint16_t MESSAGE_HELLO = MESSAGE_INVALID + 1;
const static uint16_t MESSAGE_ROUND2_REQUEST = MESSAGE_HELLO + 1;
const static uint16_t MESSAGE_ROUND2_PREPREPARE = MESSAGE_ROUND2_REQUEST + 1;
const static uint16_t MESSAGE_ROUND2_PARTIAL_COMMIT = MESSAGE_ROUND2_PREPREPARE + 1;
const static uint16_t MESSAGE_ROUND2_FULL_COMMIT = MESSAGE_ROUND2_PARTIAL_COMMIT + 1;
const static uint16_t MESSAGE_ROUND3_FETCH_REQUEST = MESSAGE_ROUND2_FULL_COMMIT + 1;
const static uint16_t MESSAGE_ROUND3_FETCH_RESPONSE = MESSAGE_ROUND3_FETCH_REQUEST + 1;

struct MessageHeader;
struct MessageHeader_BE;

struct MessageHeader{
    uint16_t version;
    uint16_t msgType;
    uint32_t payloadLen;

    static MessageHeader_BE *serialize(MessageHeader *p);
};

static_assert(sizeof(MessageHeader) == 8);

struct MessageHeader_BE{
    uint16_t version;
    uint16_t msgType;
    uint32_t payloadLen;

    static MessageHeader *deserialize(MessageHeader_BE *p);
};

static_assert(sizeof(MessageHeader_BE) == sizeof(MessageHeader));

struct MessageHello;
struct MessageHello_BE;

struct MessageHello{
    MessageHeader header;
    uint16_t BGid;
    uint16_t SLid;

    static MessageHello_BE *serialize(MessageHello *p);
    const static size_t PAYLOAD_LEN = 4;
};

static_assert(sizeof(MessageHello) == sizeof(MessageHeader) + MessageHello::PAYLOAD_LEN);

struct MessageHello_BE{
    MessageHeader_BE header;
    uint16_t BGid;
    uint16_t SLid;

    // Header already parsed
    static MessageHello *partialDeserialize(MessageHello_BE *p);
};

static_assert(sizeof(MessageHello_BE) == sizeof(MessageHello));

struct MessageRound2Request;
struct MessageRound2Request_BE;

struct MessageRound2Request{
    MessageHeader header;
    uint16_t sender;
    char content[];

    static MessageRound2Request_BE *serialize(MessageRound2Request *p);
};

static_assert(sizeof(MessageRound2Request) == sizeof(MessageHeader) + sizeof(uint16_t));

struct MessageRound2Request_BE{
    MessageHeader_BE header;
    uint16_t sender;
    char content[];

    // Header already parsed
    static MessageRound2Request *partialDeserialize(MessageRound2Request_BE *p);
};

static_assert(sizeof(MessageRound2Request_BE) == sizeof(MessageRound2Request));

struct MessageRound2Preprepare;
struct MessageRound2Preprepare_BE;

struct MessageRound2Preprepare{
    MessageHeader header;
    uint16_t sender;
    uint16_t view;
    uint16_t seq;
    uint16_t BGid;
    uint16_t requestType;
    uint16_t cycle;
    uint16_t lastcycle;
    uint16_t collector_SLid;
    uint16_t numOfRound3Participants;
    char participantsAndContent[];

    static MessageRound2Preprepare_BE *serialize(MessageRound2Preprepare *p);
};

struct MessageRound2Preprepare_BE{
    MessageHeader_BE header;
    uint16_t sender;
    uint16_t view;
    uint16_t seq;
    uint16_t BGid;
    uint16_t requestType;
    uint16_t cycle;
    uint16_t lastcycle;
    uint16_t collector_SLid;
    uint16_t numOfRound3Participants;
    char participantsAndContent[];

    static MessageRound2Preprepare *partialDeserialize(MessageRound2Preprepare_BE *p);
};

static_assert(sizeof(MessageRound2Preprepare_BE) == sizeof(MessageRound2Preprepare));

struct MessageRound2PartialCommit;
struct MessageRound2PartialCommit_BE;

struct MessageRound2PartialCommit{
    MessageHeader header;
    uint16_t sender;
    uint16_t view;
    uint16_t seq;
    char signature[SBFT_SIGNATURE_SIZE];

    static MessageRound2PartialCommit_BE *serialize(MessageRound2PartialCommit *p);
};

struct MessageRound2PartialCommit_BE{
    MessageHeader_BE header;
    uint16_t sender;
    uint16_t view;
    uint16_t seq;
    char signature[SBFT_SIGNATURE_SIZE];

    static MessageRound2PartialCommit *partialDeserialize(MessageRound2PartialCommit_BE *p);
};

static_assert(sizeof(MessageRound2PartialCommit_BE) == sizeof(MessageRound2PartialCommit));

struct MessageRound2FullCommit;
struct MessageRound2FullCommit_BE;

struct MessageRound2FullCommit{
    MessageHeader header;
    uint16_t sender;
    uint16_t view;
    uint16_t seq;
    char combinedSignature[SBFT_COMBINED_SIGNATURE_SIZE];

    static MessageRound2FullCommit_BE *serialize(MessageRound2FullCommit *p);
};

struct MessageRound2FullCommit_BE{
    MessageHeader header;
    uint16_t sender;
    uint16_t view;
    uint16_t seq;
    char combinedSignature[SBFT_COMBINED_SIGNATURE_SIZE];

    static MessageRound2FullCommit *partialDeserialize(MessageRound2FullCommit_BE *p);
};

static_assert(sizeof(MessageRound2FullCommit_BE) == sizeof(MessageRound2FullCommit));

struct CompoundHash{
    uint16_t BGid;
    uint16_t cycle;
    uint16_t lastcycle;
    char hash[SBFT_HASH_SIZE];
};

struct MessageRound3FetchRequest;
struct MessageRound3FetchRequest_BE;

struct MessageRound3FetchRequest{
    MessageHeader header;
    uint16_t sender_BGid;
    uint16_t sender_SLid;
    CompoundHash hash;
    char combinedSignature[SBFT_COMBINED_SIGNATURE_SIZE];   // over 'hash'

    static MessageRound3FetchRequest_BE* serialize(MessageRound3FetchRequest *p);
};

struct MessageRound3FetchRequest_BE{
    MessageHeader header;
    uint16_t sender_BGid;
    uint16_t sender_SLid;
    CompoundHash hash;
    char combinedSignature[SBFT_COMBINED_SIGNATURE_SIZE];   // over 'hash'

    static MessageRound3FetchRequest* partialDeserialize(MessageRound3FetchRequest_BE *p);
};

struct MessageRound3FetchResponse;
struct MessageRound3FetchResponse_BE;

struct MessageRound3FetchResponse{
    MessageHeader header;
    uint16_t sender_BGid;
    uint16_t sender_SLid;
    char combinedSignature[SBFT_COMBINED_SIGNATURE_SIZE];   // over 'entirePreprepareMsg'
    char entirePreprepareMsg[];

    static MessageRound3FetchResponse_BE *serialize(MessageRound3FetchResponse *p);
};

struct MessageRound3FetchResponse_BE{
    MessageHeader header;
    uint16_t sender_BGid;
    uint16_t sender_SLid;
    char combinedSignature[SBFT_COMBINED_SIGNATURE_SIZE];   // over 'entirePreprepareMsg'
    char entirePreprepareMsg[];

    static MessageRound3FetchResponse *partialDeserialize(MessageRound3FetchResponse_BE *p);
};

static_assert(sizeof(MessageRound3FetchResponse) == sizeof(MessageRound3FetchResponse_BE));

struct ConnectivityInfo{
    uint16_t BGid;
    uint16_t totalBGnum;
    bool reachable[];
};

struct MessageRound3Connectivity;
struct MessageRound3Connectivity_BE;

struct MessageRound3Connectivity{
    MessageHeader header;
    uint16_t sender_BGid;
    uint16_t sender_SLid;
    char combinedSignature[SBFT_COMBINED_SIGNATURE_SIZE];   // over 'info'
    ConnectivityInfo info;

    static MessageRound3Connectivity_BE *serialize(MessageRound3Connectivity *p);
};

struct MessageRound3Connectivity_BE{
    MessageHeader header;
    uint16_t sender_BGid;
    uint16_t sender_SLid;
    char combinedSignature[SBFT_COMBINED_SIGNATURE_SIZE];   // over 'info'
    ConnectivityInfo info;

    static MessageRound3Connectivity *partialDeserialize(MessageRound3Connectivity_BE *p);
};

static_assert(sizeof(MessageRound3Connectivity_BE) == sizeof(MessageRound3Connectivity));

struct MessageRound3FullMembership;
struct MessageRound3FullMembership_BE;

struct MessageRound3FullMembership{
    MessageHeader header;
    uint16_t sender_BGid;
    uint16_t sender_SLid;
    char combinedSignature[SBFT_COMBINED_SIGNATURE_SIZE];   // over 'totalBGnum' & 'connectivityAndSignature'
    uint16_t totalBGnum;
    char connectivityAndSignature[];

    static MessageRound3FullMembership_BE *serialize(MessageRound3FullMembership *p);
};

struct MessageRound3FullMembership_BE{
    MessageHeader header;
    uint16_t sender_BGid;
    uint16_t sender_SLid;
    char combinedSignature[SBFT_COMBINED_SIGNATURE_SIZE];   // over 'totalBGnum' & 'connectivityAndSignature'
    uint16_t totalBGnum;
    char connectivityAndSignature[];

    static MessageRound3FullMembership *partialDeserialize(MessageRound3FullMembership_BE *p);
};

static_assert(sizeof(MessageRound3FullMembership_BE) == sizeof(MessageRound3FullMembership));

#pragma pack(pop)

size_t getMessageSize(MessageHeader *pHeader);
MessageRound3FetchResponse* getRound3Response_caller_free_mem(MessageRound2Preprepare *pPreprepare, MessageRound2FullCommit *pFullC, uint16_t SLid);
MessageRound3FetchRequest* getRound3Request_caller_free_mem(MessageRound3FetchResponse *pResponse);

#endif
