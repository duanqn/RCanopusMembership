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

MessageRound3FetchResponse* getRound3Response_caller_free_mem(MessageRound2Preprepare *pPreprepare, MessageRound2FullCommit *pFullC, uint16_t SLid, uint16_t cycle){
    size_t totalSize = sizeof(MessageRound3FetchResponse) + getMessageSize((MessageHeader *)pPreprepare);
    char *buffer = new char[totalSize];
    #ifdef MEM_DBG
    heapalloc.fetch_add(totalSize);
    #endif
    MessageRound3FetchResponse *pResponse = (MessageRound3FetchResponse *)buffer;

    pResponse->header.msgType = MESSAGE_ROUND3_FETCH_RESPONSE;
    pResponse->header.version = pPreprepare->header.version;
    pResponse->header.payloadLen = totalSize - sizeof(MessageHeader);

    pResponse->sender_BGid = pPreprepare->BGid;
    pResponse->sender_SLid = SLid; // caller need to fill in
    pResponse->cycle = cycle;
    memcpy(pResponse->combinedSignature, pFullC->combinedSignature, SBFT_COMBINED_SIGNATURE_SIZE);
    memcpy(pResponse->entirePreprepareMsg, pPreprepare, getMessageSize((MessageHeader *)pPreprepare));

    return pResponse;
}

MessageRound3FetchRequest* getRound3Request_caller_free_mem(MessageRound3FetchResponse *pResponse){
    MessageRound3FetchRequest *pRequest = (MessageRound3FetchRequest *)new char[sizeof(MessageRound3FetchRequest)];
    #ifdef MEM_DBG
    heapalloc.fetch_add(sizeof(MessageRound3FetchRequest));
    #endif
    pRequest->header.version = pResponse->header.version;
    pRequest->header.msgType = MESSAGE_ROUND3_FETCH_REQUEST;
    pRequest->header.payloadLen = sizeof(MessageRound3FetchRequest) - sizeof(MessageHeader);

    pRequest->sender_BGid = pResponse->sender_BGid;
    pRequest->sender_SLid = pResponse->sender_SLid;
    pRequest->hash.BGid = pRequest->sender_BGid;
    MessageRound2Preprepare *pPreprepare = (MessageRound2Preprepare *)pResponse->entirePreprepareMsg;
    pRequest->hash.cycle = pPreprepare->cycle;
    pRequest->hash.lastcycle = pPreprepare->lastcycle;
    pRequest->cycle = pResponse->cycle;
    DebugThrow(pRequest->cycle > pRequest->hash.lastcycle && pRequest->cycle <= pRequest->hash.cycle);
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

MessageRound3FetchRequest_BE *MessageRound3FetchRequest::serialize(MessageRound3FetchRequest *p){
    MessageHeader::serialize(&p->header);
    toBE(&p->sender_BGid);
    toBE(&p->sender_SLid);
    toBE(&p->cycle);
    toBE(&p->hash.BGid);
    toBE(&p->hash.cycle);
    toBE(&p->hash.lastcycle);

    return (MessageRound3FetchRequest_BE *)p;
}

MessageRound3FetchRequest *MessageRound3FetchRequest_BE::partialDeserialize(MessageRound3FetchRequest_BE *p){
    fromBE(&p->sender_BGid);
    fromBE(&p->sender_SLid);
    fromBE(&p->cycle);
    fromBE(&p->hash.BGid);
    fromBE(&p->hash.cycle);
    fromBE(&p->hash.lastcycle);

    return (MessageRound3FetchRequest *)p;
}

MessageRound3FetchResponse_BE *MessageRound3FetchResponse::serialize(MessageRound3FetchResponse *p){
    MessageHeader::serialize(&p->header);
    toBE(&p->sender_BGid);
    toBE(&p->sender_SLid);
    toBE(&p->cycle);

    return (MessageRound3FetchResponse_BE *)p;
}

MessageRound3FetchResponse *MessageRound3FetchResponse_BE::partialDeserialize(MessageRound3FetchResponse_BE *p){
    fromBE(&p->sender_BGid);
    fromBE(&p->sender_SLid);
    fromBE(&p->cycle);

    return (MessageRound3FetchResponse *)p;
}

MessageRound3GeneralFetch_BE * MessageRound3GeneralFetch::serialize(MessageRound3GeneralFetch *p){
    MessageHeader::serialize(&p->header);
    toBE(&p->sender_BGid);
    toBE(&p->sender_SLid);
    toBE(&p->cycle);
    toBE(&p->msgTypeDemanded);

    return (MessageRound3GeneralFetch_BE *)p;
}

MessageRound3GeneralFetch * MessageRound3GeneralFetch_BE::partialDeserialize(MessageRound3GeneralFetch_BE *p){
    fromBE(&p->sender_BGid);
    fromBE(&p->sender_SLid);
    fromBE(&p->cycle);
    fromBE(&p->msgTypeDemanded);

    return (MessageRound3GeneralFetch *)p;
}

MessageRound3FullMembership_BE * MessageRound3FullMembership::serialize(MessageRound3FullMembership *p){
    MessageHeader::serialize(&p->header);
    toBE(&p->sender_BGid);
    toBE(&p->sender_SLid);
    toBE(&p->cycle);
    toBE(&p->totalBGnum);

    return (MessageRound3FullMembership_BE *)p;
}

MessageRound3FullMembership * MessageRound3FullMembership_BE::partialDeserialize(MessageRound3FullMembership_BE *p){
    fromBE(&p->sender_BGid);
    fromBE(&p->sender_SLid);
    fromBE(&p->cycle);
    fromBE(&p->totalBGnum);

    return (MessageRound3FullMembership *)p;
}

MessageRound3PreprepareBaseline_BE *MessageRound3PreprepareBaseline::serialize(MessageRound3PreprepareBaseline *p){
    MessageHeader::serialize(&p->header);
    toBE(&p->sender);
    toBE(&p->cycle);
    toBE(&p->collector_BGid);

    return (MessageRound3PreprepareBaseline_BE *)p;
}

MessageRound3PreprepareBaseline * MessageRound3PreprepareBaseline_BE::partialDeserialize(MessageRound3PreprepareBaseline_BE *p){
    fromBE(&p->sender);
    fromBE(&p->cycle);
    fromBE(&p->collector_BGid);

    return (MessageRound3PreprepareBaseline *)p;
}

MessageRound3PartialCommitBaseline_BE *MessageRound3PartialCommitBaseline::serialize(MessageRound3PartialCommitBaseline *p){
    MessageHeader::serialize(&p->header);
    toBE(&p->sender);
    toBE(&p->cycle);

    return (MessageRound3PartialCommitBaseline_BE *)p;
}

MessageRound3PartialCommitBaseline * MessageRound3PartialCommitBaseline_BE::partialDeserialize(MessageRound3PartialCommitBaseline_BE *p){
    fromBE(&p->sender);
    fromBE(&p->cycle);

    return (MessageRound3PartialCommitBaseline *)p;
}

MessageRound3FullCommitBaseline_BE *MessageRound3FullCommitBaseline::serialize(MessageRound3FullCommitBaseline *p){
    MessageHeader::serialize(&p->header);
    toBE(&p->sender);
    toBE(&p->cycle);

    return (MessageRound3FullCommitBaseline_BE *)p;
}

MessageRound3FullCommitBaseline * MessageRound3FullCommitBaseline_BE::partialDeserialize(MessageRound3FullCommitBaseline_BE *p){
    fromBE(&p->sender);
    fromBE(&p->cycle);

    return (MessageRound3FullCommitBaseline *)p;
}
