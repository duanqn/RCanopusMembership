#include "message.h"

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

MessageRound2Preprepare_BE *MessageRound2Preprepare::serialize(MessageRound2Preprepare *p){
    MessageHeader::serialize(&p->header);
    toBE(&p->sender);
    toBE(&p->view);
    toBE(&p->seq);
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