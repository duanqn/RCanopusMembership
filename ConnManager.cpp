#include "ConnManager.h"
#include "const.h"

char * ConnManager::recvMessage_caller_free_mem(int sock){
    char headerBuf[sizeof(MessageHeader)];
    PeerConnection::recv(sock, sizeof(MessageHeader), headerBuf);
    MessageHeader_BE *pbeHeader = (MessageHeader_BE *)headerBuf;
    MessageHeader *pHeader = MessageHeader_BE::deserialize(pbeHeader);

    size_t extraLen = pHeader->payloadLen;
    char* buffer = new char[sizeof(MessageHeader) + extraLen];
    memcpy(buffer, headerBuf, sizeof(MessageHeader));
    char *payload = buffer + sizeof(MessageHeader);

    PeerConnection::recv(sock, extraLen, payload);

    switch(pHeader->msgType){
        case MESSAGE_HELLO:
        MessageHello_BE::partialDeserialize((MessageHello_BE *)buffer);
        break;
        case MESSAGE_ROUND2_REQUEST:
        MessageRound2Request_BE::partialDeserialize((MessageRound2Request_BE *)buffer);
        break;
        default:
        throw Exception(Exception::EXCEPTION_MESSAGE_INVALID_TYPE);
        break;
    }

    return buffer;
}

int ConnManager::listenForIncomingConnections(int boundSocket){
    if(listen(boundSocket, LISTEN_BACKLOG) == -1){
        return -1;
    }

    AlgoLib::Util::TCleanup t([&boundSocket]{
        printf("Closing the listening socket\n");
        close(boundSocket);
    });

    int expectedConnections = 0;
    int BGnum = m_upConfig->rgrgPeerAddr.size();
    for(int bg = 0; bg < BGnum; ++bg){
        for(int sl = 0; sl < m_upConfig->rgrgPeerAddr[bg].size(); ++sl){
            if(bg == m_upConfig->BGid && sl == m_upConfig->SLid){
                continue;
            }

            if(isPassiveConnection(bg, sl)){
                ++expectedConnections;
            }
        }
    }

    struct sockaddr_in remoteAddr;
    socklen_t remoteAddrLen = sizeof(remoteAddr);

    // Loop 0
    for(int i = 0; i < expectedConnections; ++i){
        int newSocket = accept(boundSocket, (sockaddr *)&remoteAddr, &remoteAddrLen);
        if(newSocket == -1){
            continue;
        }

        // Loop 1
        do{
            char *buffer = nullptr;
            try{
                buffer = recvMessage_caller_free_mem(newSocket);
            }
            catch(Exception &e){
                // buffer is still nullptr, no need to free
                switch(e.getReason()){
                    case Exception::EXCEPTION_MESSAGE_INVALID_TYPE:
                    [[fallthrough]];
                    case Exception::EXCEPTION_MESSAGE_INVALID_VERSION:
                    continue;
                }
            }

            // buffer is not null now            
            AlgoLib::Util::TCleanup t([buffer]{
                delete[] buffer;
            });

            MessageHeader *pHeader = (MessageHeader *)buffer;

            if(pHeader->msgType != MESSAGE_HELLO){
                continue;
            }

            MessageHello *pHello = (MessageHello *)buffer;

            rgrgConnection[pHello->BGid][pHello->SLid].fdSocket = newSocket;

            break;
        }while(true);
    }

    return 0;
}

ConnManager::ConnManager(const Config &conf):
    pRecvQueue(nullptr),
    pSendQueue(nullptr),
    numOfRemotePeers(0),
    m_upConfig(std::make_unique<Config>(conf)),
    rgrgConnection(nullptr),
    rgPoll(nullptr),
    rgPollSlotToConnection(nullptr){

    int BGnum = m_upConfig->rgrgPeerAddr.size();
    rgrgConnection = new PeerConnection*[BGnum];
    for(int i = 0; i < BGnum; ++i){
        rgrgConnection[i] = new PeerConnection[m_upConfig->rgrgPeerAddr[i].size()];
    }

    // Construct local Peer
    std::shared_ptr<Peer> spLocal = std::make_shared<Peer>(m_upConfig->BGid, m_upConfig->SLid);
    spLocal->m_addr = m_upConfig->rgrgPeerAddr[m_upConfig->BGid][m_upConfig->SLid];

    // Create and bind listening socket
    int socketListen = socket(AF_INET, SOCK_STREAM, 0);
    if(socketListen == -1){
        throw Exception(Exception::EXCEPTION_SOCKET_CREATION);
    }

    if(bind(socketListen, (sockaddr *)&(spLocal->m_addr), sizeof(spLocal->m_addr)) == -1){
        throw Exception(Exception::EXCEPTION_SOCKET_BINDING);
    }

    std::function<int(int)> funcBind = std::bind(&ConnManager::listenForIncomingConnections, this, std::placeholders::_1);
    AlgoLib::Util::AsyncExecution jobListen(std::forward<decltype(funcBind)>(funcBind), std::forward<int>(socketListen));

    // Build connection table
    for(int bg = 0; bg < BGnum; ++bg){
        for(int sl = 0; sl < m_upConfig->rgrgPeerAddr[bg].size(); ++sl){
            if(bg == m_upConfig->BGid && sl == m_upConfig->SLid){
                continue;
            }

            this->numOfRemotePeers++;

            // Construct remote peer
            std::unique_ptr<Peer> upPeer = std::make_unique<Peer>(bg, sl);
            upPeer->m_addr = m_upConfig->rgrgPeerAddr[bg][sl];
            rgrgConnection[bg][sl].m_upRemotePeer = std::move(upPeer);
            rgrgConnection[bg][sl].m_spLocalPeer = spLocal;

            if(isActiveConnection(bg, sl)){
                rgrgConnection[bg][sl].fdSocket = socket(AF_INET, SOCK_STREAM, 0);
                if(rgrgConnection[bg][sl].fdSocket == -1){
                    throw Exception(Exception::EXCEPTION_SOCKET_CREATION);
                }
                // Retry until succeeds
                while(connect(rgrgConnection[bg][sl].fdSocket, (sockaddr *)&(rgrgConnection[bg][sl].m_upRemotePeer->m_addr), sizeof(rgrgConnection[bg][sl].m_upRemotePeer->m_addr)) == -1);

                MessageHello dont_care; // Always operate with pointers
                MessageHello *pHello = &dont_care;
                pHello->header.version = VERSION_LATEST;
                pHello->header.msgType = MESSAGE_HELLO;
                pHello->header.payloadLen = pHello->PAYLOAD_LEN;
                pHello->BGid = m_upConfig->BGid;
                pHello->SLid = m_upConfig->SLid;

                MessageHello_BE *pbeHello = MessageHello::serialize(pHello);
                rgrgConnection[bg][sl].send((char *)pbeHello, sizeof(*pbeHello));
            }
        }
    }

    jobListen.waitForResult();

    pRecvQueue = new moodycamel::BlockingReaderWriterQueue<QueueElement>(INIT_QUEUE_CAPACITY);
    pSendQueue = new moodycamel::BlockingConcurrentQueue<QueueElement>(INIT_QUEUE_CAPACITY);

    rgPoll = new struct pollfd[numOfRemotePeers + 1];
    rgPollSlotToConnection = new PeerConnection*[numOfRemotePeers];
    int slot = 0;

    for(int bg = 0; bg < BGnum; ++bg){
        for(int sl = 0; sl < m_upConfig->rgrgPeerAddr[bg].size(); ++sl){
            if(bg == m_upConfig->BGid && sl == m_upConfig->SLid){
                continue;
            }

            rgPollSlotToConnection[slot] = &rgrgConnection[bg][sl];
            rgPoll[slot].fd = rgrgConnection[bg][sl].fdSocket;
            rgPoll[slot].events = POLLIN;
            rgPoll[slot].revents = 0;
            ++slot;
        }
    }

}

void ConnManager::listener(){
    int indexExit = numOfRemotePeers;
    while(true){
        // TODO: listen for (numOfRemotePeers + 1) fds
        poll(rgPoll, numOfRemotePeers, -1);

        // TODO: check rgPoll[indexExit] first

        for(int i = 0; i < numOfRemotePeers; ++i){
            if(rgPoll[i].revents != 0){
                if(rgPoll[i].revents & POLLIN != 0){
                    // recv packet
                    char *pMessage = recvMessage_caller_free_mem(rgPoll[i].fd);
                    QueueElement element;
                    element.pMessage = (MessageHeader *)pMessage;
                    element.upPeers->push_front(rgPollSlotToConnection[i]);
                    pRecvQueue->enqueue(std::move(element));
                }
                else if(rgPoll[i].revents & POLLHUP != 0){
                    close(rgPoll[i].fd);
                    rgPoll[i].fd = -rgPoll[i].fd;
                }
                else{
                    // Error
                }

                rgPoll[i].revents = 0;
            }
        }
    }
}

void ConnManager::mockClient(std::chrono::milliseconds interval){
    int BGid = m_upConfig->BGid;
    PeerConnection *pLeader = &rgrgConnection[BGid][0];
    while(true){
        size_t size = sizeof(MessageHeader) + REQUEST_BATCH_SIZE;
        char *buffer = new char[size];
        MessageHeader *pHeader = (MessageHeader *)buffer;
        pHeader->version = VERSION_LATEST;
        pHeader->msgType = MESSAGE_ROUND2_REQUEST;
        pHeader->payloadLen = REQUEST_BATCH_SIZE;

        QueueElement element;
        element.pMessage = pHeader;
        element.upPeers->push_front(pLeader);
        
        pSendQueue->enqueue(std::move(element));
        std::this_thread::sleep_for(interval);
    }
}

void ConnManager::sender(){
    while(true){
        // TODO: Check signal for exit

        QueueElement element;
        if(!pSendQueue->try_dequeue(element)){
            continue;
        }

        AlgoLib::Util::TCleanup t([&element]{
            delete[] (char *)element.pMessage;
        });

        while(!element.upPeers->empty()){
            PeerConnection *pPeer = element.upPeers->front();
            element.upPeers->pop_front();

            size_t totalLength = sizeof(MessageHeader) + element.pMessage->payloadLen;

            // Convert byte order
            switch(element.pMessage->msgType){
                case MESSAGE_HELLO:
                MessageHello::serialize((MessageHello *)element.pMessage);
                break;

                case MESSAGE_ROUND2_REQUEST:
                MessageRound2Request::serialize((MessageRound2Request *)element.pMessage);
                break;

                default:
                throw Exception(Exception::EXCEPTION_MESSAGE_INVALID_TYPE);
            }
            
            pPeer->send((char *)element.pMessage, totalLength);
        }
    }
}

void ConnManager::start(){
    std::function<void(void)> funcListen = std::bind(&ConnManager::listener, this);
    AlgoLib::Util::AsyncExecution listenerJob(std::forward<decltype(funcListen)>(funcListen));
    std::function<void(void)> funcSend = std::bind(&ConnManager::sender, this);
    AlgoLib::Util::AsyncExecution senderJob(std::forward<decltype(funcSend)>(funcSend));

    std::function<void(void)> funcClient = std::bind(&ConnManager::mockClient, this, REQUEST_BATCH_INTERVAL);
    AlgoLib::Util::AsyncExecution<std::function<void(void)>> *pClientJob = nullptr;
    if(m_upConfig->SLid != 0){
        // Not BG leader
        pClientJob = new AlgoLib::Util::AsyncExecution<std::function<void(void)>> (std::forward<std::function<void(void)>>(funcClient));
    }

    AlgoLib::Util::TCleanup t([&pClientJob]{
        if(pClientJob != nullptr){
            delete pClientJob;
        }
    });

    dispatcher_test();
}

void ConnManager::dispatcher_test(){
    while(true){
        QueueElement element;
        pRecvQueue->wait_dequeue(element);

        AlgoLib::Util::TCleanup t([&element]{
            delete[] (char *)element.pMessage;
        });

        PeerConnection *pSender = element.upPeers->front();

        printf("Received message.\nFrom: BG %d SL %d\nType: %hu\n", pSender->m_upRemotePeer->m_BGid, pSender->m_upRemotePeer->m_SLid, element.pMessage->msgType);
    }
}

