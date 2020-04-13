#ifndef QUEUE_ELEMENT_H_
#define QUEUE_ELEMENT_H_

#include "message.h"
#include "PeerConnection.h"
#include <utility>
#include <forward_list>

struct QueueElement final{
    std::unique_ptr<std::forward_list<PeerConnection*> > upPeers;
    MessageHeader* pMessage;

    QueueElement():
        pMessage(nullptr),
        upPeers(std::make_unique<std::forward_list<PeerConnection*> >())
        {}

    QueueElement(const QueueElement &) = delete;
    QueueElement& operator=(const QueueElement& e) = delete;

    QueueElement(QueueElement&& e):
        pMessage(e.pMessage),
        upPeers(std::move(e.upPeers))
        {}

    QueueElement& operator=(QueueElement&& e){
        pMessage = std::move(e.pMessage);
        upPeers = std::move(e.upPeers);
    }

    ~QueueElement(){}
};

#endif
