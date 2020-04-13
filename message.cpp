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

    return (MessageRound2Request_BE *)p;
}

MessageRound2Request *MessageRound2Request_BE::partialDeserialize(MessageRound2Request_BE *p){
    return (MessageRound2Request *)p;
}