#ifndef QUEUE_ELEMENT_H_
#define QUEUE_ELEMENT_H_

#include "message.h"
#include "PeerConnection.h"
#include <utility>
#include <forward_list>
#include <cstring>

struct QueueElement final{
    private:
    MessageHeader* release_message(){
        MessageHeader *ret = pMessage;
        pMessage = nullptr;
        return ret;
    }

    public:
    std::unique_ptr<std::forward_list<PeerConnection*> > upPeers;
    MessageHeader* pMessage;

    QueueElement():
        pMessage(nullptr),
        upPeers(std::make_unique<std::forward_list<PeerConnection*> >())
        {}

    QueueElement(const QueueElement &) = delete;
    QueueElement& operator=(const QueueElement& e) = delete;

    QueueElement(QueueElement&& e):
        pMessage(e.release_message()),
        upPeers(std::move(e.upPeers))
        {}

    QueueElement& operator=(QueueElement&& e){
        pMessage = e.release_message();
        upPeers = std::move(e.upPeers);
    }

    ~QueueElement(){
        if(pMessage != nullptr){
            delete[] (char *)pMessage;
            #ifdef MEM_DBG
            heapalloc.fetch_sub(getMessageSize(pMessage));
            #endif
        }
    }

    void clone(const QueueElement &e){
        size_t messageSize = getMessageSize(e.pMessage);
        char *buffer = new char[messageSize];
        #ifdef MEM_DBG
        heapalloc.fetch_add(messageSize);
        #endif

        // Free any occupied memory
        if(pMessage != nullptr){
            delete[] (char *)pMessage;
            #ifdef MEM_DBG
            heapalloc.fetch_sub(getMessageSize(pMessage));
            #endif
        }

        pMessage = (MessageHeader *)buffer;
        memcpy(buffer, e.pMessage, messageSize);

        upPeers = std::make_unique<std::forward_list<PeerConnection*> >(*(e.upPeers));
    }
};

#endif
