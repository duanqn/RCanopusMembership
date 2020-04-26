#include "message.h"
#include <cstring>

MessageHeader_BE *MessageHeader::serialize(MessageHeader *p){
    toBE(&p->version);
    toBE(&p->msgType);
    toBE(&p->payloadLen);

    return (MessageHeader_BE *)p;
}

MessageHeader *MessageHeader_BE::deserialize(MessageHeader_BE *p){
    fromBE(&p->version);
    fromBE(&p->msgType);
    fromBE(&p->payloadLen);

    return (MessageHeader *)p;
}

MessageHello_BE *MessageHello::serialize(MessageHello *p){
    MessageHeader::serialize(&p->header);
    toBE(&p->BGid);
    toBE(&p->SLid);

    return (MessageHello_BE *)p;
}

MessageHello *MessageHello_BE::partialDeserialize(MessageHello_BE *p){
    toBE(&p->BGid);
    toBE(&p->SLid);

    return (MessageHello *)p;
}

MessageRound2Request_BE *MessageRound2Request::serialize(MessageRound2Request *p){
    MessageHeader::serialize(&p->header);
    toBE(&p->sender);

    return (MessageRound2Request_BE *)p;
}

MessageRound2Request *MessageRound2Request_BE::partialDeserialize(MessageRound2Request_BE *p){
    fromBE(&p->sender);
    return (MessageRound2Request *)p;
}

size_t getMessageSize(MessageHeader *pHeader){
    return sizeof(MessageHeader) + pHeader->payloadLen;
}

MessageRound3FetchResponse* getRound3Response_caller_free_mem(MessageRound2Preprepare *pPreprepare, MessageRound2FullCommit *pFullC, uint16_t SLid){
    size_t totalSize = sizeof(MessageRound3FetchResponse) + getMessageSize((MessageHeader *)pPreprepare);
    char *buffer = new char[totalSize];
    MessageRound3FetchResponse *pResponse = (MessageRound3FetchResponse *)buffer;

    pResponse->header.msgType = MESSAGE_ROUND3_FETCH_RESPONSE;
    pResponse->header.version = pPreprepare->header.version;
    pResponse->header.payloadLen = totalSize - sizeof(MessageHeader);

    pResponse->sender_BGid = pPreprepare->BGid;
    pResponse->sender_SLid = SLid; // caller need to fill in
    memcpy(pResponse->combinedSignature, pFullC->combinedSignature, SBFT_COMBINED_SIGNATURE_SIZE);
    memcpy(pResponse->entirePreprepareMsg, pPreprepare, getMessageSize((MessageHeader *)pPreprepare));

    return pResponse;
}

MessageRound3FetchRequest* getRound3Request_caller_free_mem(MessageRound3FetchResponse *pResponse){
    MessageRound3FetchRequest *pRequest = (MessageRound3FetchRequest *)new char[sizeof(MessageRound3FetchRequest)];
    pRequest->header.version = pResponse->header.version;
    pRequest->header.msgType = MESSAGE_ROUND3_FETCH_REQUEST;
    pRequest->header.payloadLen = sizeof(MessageRound3FetchRequest) - sizeof(MessageHeader);

    pRequest->sender_BGid = pResponse->sender_BGid;
    pRequest->sender_SLid = pResponse->sender_SLid;
    pRequest->hash.BGid = pRequest->sender_BGid;
    MessageRound2Preprepare *pPreprepare = (MessageRound2Preprepare *)pResponse->entirePreprepareMsg;
    pRequest->hash.cycle = pPreprepare->cycle;
    pRequest->hash.lastcycle = pPreprepare->lastcycle;
    memset(pRequest->hash.hash, 0, SBFT_HASH_SIZE);
    memcpy(pRequest->combinedSignature, pResponse->combinedSignature, SBFT_COMBINED_SIGNATURE_SIZE);

    return pRequest;
}

MessageRound2Preprepare_BE *MessageRound2Preprepare::serialize(MessageRound2Preprepare *p){
    MessageHeader::serialize(&p->header);
    toBE(&p->sender);
    toBE(&p->view);
    toBE(&p->seq);
    toBE(&p->BGid);
    toBE(&p->requestType);
    toBE(&p->cycle);
    toBE(&p->lastcycle);
    toBE(&p->collector_SLid);
    uint16_t *pField = (uint16_t *)p->participantsAndContent;
    for(uint16_t i = 0; i < p->numOfRound3Participants; ++i){
        toBE(pField);
        pField++;
    }

    toBE(&p->numOfRound3Participants);

    return (MessageRound2Preprepare_BE *)p;
}

MessageRound2Preprepare *MessageRound2Preprepare_BE::partialDeserialize(MessageRound2Preprepare_BE *p){
    fromBE(&p->sender);
    fromBE(&p->view);
    fromBE(&p->seq);
    fromBE(&p->BGid);
    fromBE(&p->requestType);
    fromBE(&p->cycle);
    fromBE(&p->lastcycle);
    fromBE(&p->collector_SLid);
    fromBE(&p->numOfRound3Participants);
    uint16_t *pField = (uint16_t *)p->participantsAndContent;
    for(uint16_t i = 0; i < p->numOfRound3Participants; ++i){
        fromBE(pField);
        pField++;
    }

    return (MessageRound2Preprepare *)p;
}

MessageRound2PartialCommit_BE *MessageRound2PartialCommit::serialize(MessageRound2PartialCommit *p){
    MessageHeader::serialize(&p->header);
    toBE(&p->sender);
    toBE(&p->view);
    toBE(&p->seq);

    return (MessageRound2PartialCommit_BE *)p;
}

MessageRound2PartialCommit *MessageRound2PartialCommit_BE::partialDeserialize(MessageRound2PartialCommit_BE *p){
    fromBE(&p->sender);
    fromBE(&p->view);
    fromBE(&p->seq);

    return (MessageRound2PartialCommit *)p;
}

MessageRound2FullCommit_BE *MessageRound2FullCommit::serialize(MessageRound2FullCommit *p){
    MessageHeader::serialize(&p->header);
    toBE(&p->sender);
    toBE(&p->view);
    toBE(&p->seq);

    return (MessageRound2FullCommit_BE *)p;
}

MessageRound2FullCommit *MessageRound2FullCommit_BE::partialDeserialize(MessageRound2FullCommit_BE *p){
    fromBE(&p->sender);
    fromBE(&p->view);
    fromBE(&p->seq);

    return (MessageRound2FullCommit *)p;
}
