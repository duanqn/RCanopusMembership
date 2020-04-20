#include "ConnManager.h"
#include "const.h"
#include <sched.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

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

        case MESSAGE_ROUND2_PREPREPARE:
        MessageRound2Preprepare_BE::partialDeserialize((MessageRound2Preprepare_BE *)buffer);
        break;

        case MESSAGE_ROUND2_PARTIAL_COMMIT:
        MessageRound2PartialCommit_BE::partialDeserialize((MessageRound2PartialCommit_BE *)buffer);
        break;

        case MESSAGE_ROUND2_FULL_COMMIT:
        MessageRound2FullCommit_BE::partialDeserialize((MessageRound2FullCommit_BE *)buffer);
        break;

        default:
        printf("Message type %hu not allowed to recv.\n", pHeader->msgType);
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
    int BGnum = m_upConfig->rgBGInfo.size();
    for(int bg = 0; bg < BGnum; ++bg){
        for(int sl = 0; sl < m_upConfig->numSL(bg); ++sl){
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

        int booltrue = 1;
        socklen_t optlen = sizeof(booltrue);
        if(setsockopt(newSocket, IPPROTO_TCP, TCP_NODELAY, &booltrue, optlen) == -1){
            throw Exception(Exception::EXCEPTION_SOCKET_OPT);
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
    REQUEST_BATCH_INTERVAL(0),
    numOfRemotePeers(0),
    m_upConfig(std::make_unique<Config>(conf)),
    rgrgConnection(nullptr),
    rgPoll(nullptr),
    rgPollSlotToConnection(nullptr),
    rgLeader_batch_received_from(nullptr),
    leader_batch_processed(0),
    pRound2_current_status(nullptr),
    leader_round2_next_rcanopus_cycle(1u),
    round2_next_view(1u),
    round2_next_seq(1u),
    mapRound3Status(),
    pTemporaryStorageOfPreprepare(nullptr),
    leader_envoys_rr(1),
    leader_collector_rr(1),
    round2_verifier(nullptr){

    float REQUEST_PER_BATCH = REQUEST_BATCH_SIZE / (float) REQUEST_SIZE;
    REQUEST_BATCH_INTERVAL = std::chrono::milliseconds((int)(1000 * REQUEST_PER_BATCH / m_upConfig->requestPerSLPerSecond));

    printf("interval = %ldms\n", REQUEST_BATCH_INTERVAL.count());

    int BGnum = m_upConfig->numBG();
    rgrgConnection = new PeerConnection*[BGnum];
    for(int i = 0; i < BGnum; ++i){
        rgrgConnection[i] = new PeerConnection[m_upConfig->numSL(i)];
    }

    // Construct local Peer
    std::shared_ptr<Peer> spLocal = std::make_shared<Peer>(m_upConfig->BGid, m_upConfig->SLid);
    spLocal->m_addr = m_upConfig->rgBGInfo[m_upConfig->BGid].addr[m_upConfig->SLid];

    // Create and bind listening socket
    int socketListen = socket(AF_INET, SOCK_STREAM, 0);
    if(socketListen == -1){
        throw Exception(Exception::EXCEPTION_SOCKET_CREATION);
    }

    int booltrue = 1;
    socklen_t lentrue = sizeof(booltrue);
    if(setsockopt(socketListen, SOL_SOCKET, SO_REUSEADDR, &booltrue, lentrue) == -1){
        throw Exception(Exception::EXCEPTION_SOCKET_OPT);
    }

    if(bind(socketListen, (sockaddr *)&(spLocal->m_addr), sizeof(spLocal->m_addr)) == -1){
        throw Exception(Exception::EXCEPTION_SOCKET_BINDING);
    }

    std::function<int(int)> funcBind = std::bind(&ConnManager::listenForIncomingConnections, this, std::placeholders::_1);
    AlgoLib::Util::AsyncExecution jobListen(std::forward<decltype(funcBind)>(funcBind), std::forward<int>(socketListen));

    // Build connection table
    for(int bg = 0; bg < BGnum; ++bg){
        for(int sl = 0; sl < m_upConfig->numSL(bg); ++sl){
            if(bg == m_upConfig->BGid && sl == m_upConfig->SLid){
                continue;
            }

            this->numOfRemotePeers++;

            // Construct remote peer
            std::unique_ptr<Peer> upPeer = std::make_unique<Peer>(bg, sl);
            upPeer->m_addr = m_upConfig->rgBGInfo[bg].addr[sl];
            rgrgConnection[bg][sl].m_upRemotePeer = std::move(upPeer);
            rgrgConnection[bg][sl].m_spLocalPeer = spLocal;

            if(isActiveConnection(bg, sl)){
                rgrgConnection[bg][sl].fdSocket = socket(AF_INET, SOCK_STREAM, 0);
                if(rgrgConnection[bg][sl].fdSocket == -1){
                    throw Exception(Exception::EXCEPTION_SOCKET_CREATION);
                }
                // Retry until succeeds
                while(connect(rgrgConnection[bg][sl].fdSocket, (sockaddr *)&(rgrgConnection[bg][sl].m_upRemotePeer->m_addr), sizeof(rgrgConnection[bg][sl].m_upRemotePeer->m_addr)) == -1);

                int booltrue = 1;
                socklen_t optlen = sizeof(booltrue);
                if(setsockopt(rgrgConnection[bg][sl].fdSocket, IPPROTO_TCP, TCP_NODELAY, &booltrue, optlen) == -1){
                    throw Exception(Exception::EXCEPTION_SOCKET_OPT);
                }

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

    pRecvQueue = new moodycamel::BlockingConcurrentQueue<QueueElement>(INIT_QUEUE_CAPACITY);
    pSendQueue = new moodycamel::BlockingConcurrentQueue<QueueElement>(INIT_QUEUE_CAPACITY);

    rgPoll = new struct pollfd[numOfRemotePeers + 1];
    rgPollSlotToConnection = new PeerConnection*[numOfRemotePeers];
    int slot = 0;

    for(int bg = 0; bg < BGnum; ++bg){
        for(int sl = 0; sl < m_upConfig->numSL(bg); ++sl){
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

    if(isRound2Leader()){
        int numSL = m_upConfig->numSL(m_upConfig->BGid);
        rgLeader_batch_received_from = new int[numSL];
        for(int i = 0; i < numSL; ++i){
            rgLeader_batch_received_from[i] = 0;
        }

        leader_batch_processed = 0;
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
                    
                    // Neglected: verify sender field (if the message has such a field)
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
        size_t size = sizeof(MessageRound2Request)+ REQUEST_BATCH_SIZE;
        char *buffer = new char[size];
        MessageRound2Request *pRequest = (MessageRound2Request *)buffer;
        pRequest->header.version = VERSION_LATEST;
        pRequest->header.msgType = MESSAGE_ROUND2_REQUEST;
        pRequest->header.payloadLen = sizeof(MessageRound2Request)+ REQUEST_BATCH_SIZE - sizeof(MessageHeader);
        pRequest->sender = m_upConfig->SLid;

        QueueElement element;
        element.pMessage = (MessageHeader *)pRequest;
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

        size_t totalLength = sizeof(MessageHeader) + element.pMessage->payloadLen;

        for(auto it = element.upPeers->begin(); it != element.upPeers->end(); ++it){
            if(*it == nullptr){
                QueueElement copy;
                copy.clone(element);
                pRecvQueue->enqueue(std::move(copy));
                break;
            }
        }

        // Convert byte order
        switch(element.pMessage->msgType){
            case MESSAGE_HELLO:
            MessageHello::serialize((MessageHello *)element.pMessage);
            break;

            case MESSAGE_ROUND2_REQUEST:
            MessageRound2Request::serialize((MessageRound2Request *)element.pMessage);
            break;

            case MESSAGE_ROUND2_PREPREPARE:
            MessageRound2Preprepare::serialize((MessageRound2Preprepare *)element.pMessage);
            break;

            case MESSAGE_ROUND2_PARTIAL_COMMIT:
            MessageRound2PartialCommit::serialize((MessageRound2PartialCommit *)element.pMessage);
            break;

            case MESSAGE_ROUND2_FULL_COMMIT:
            MessageRound2FullCommit::serialize((MessageRound2FullCommit *)element.pMessage);
            break;

            default:
            printf("Message type %hu not allowed to send.\n", element.pMessage->msgType);
            throw Exception(Exception::EXCEPTION_MESSAGE_INVALID_TYPE);
        }

        while(!element.upPeers->empty()){
            PeerConnection *pPeer = element.upPeers->front();
            element.upPeers->pop_front();

            if(pPeer == nullptr){
                continue;
            }

            pPeer->send((char *)element.pMessage, totalLength);
        }
    }
}

// Only called by a leader
bool ConnManager::canStartRound2(){
    DebugThrow(m_upConfig->SLid == 0);

    if(pRound2_current_status == nullptr){
        int SLnum = m_upConfig->numSL(m_upConfig->BGid);
        for(int i = 1; i < SLnum; ++i){
            // 0 is the leader itself
            if(rgLeader_batch_received_from[i] <= leader_batch_processed){
                // next batch not ready
                return false;
            }
        }

        return true;
    }
    return false;
}

void ConnManager::start(){
    std::function<void(void)> funcListen = std::bind(&ConnManager::listener, this);
    AlgoLib::Util::AsyncExecution listenerJob(std::forward<decltype(funcListen)>(funcListen));
    std::function<void(void)> funcSend = std::bind(&ConnManager::sender, this);
    AlgoLib::Util::AsyncExecution senderJob(std::forward<decltype(funcSend)>(funcSend));

    std::function<void(void)> funcClient = std::bind(&ConnManager::mockClient, this, REQUEST_BATCH_INTERVAL);
    AlgoLib::Util::AsyncExecution<std::function<void(void)>> *pClientJob = nullptr;

    if(!isRound2Leader()){
        // Not BG leader
        pClientJob = new AlgoLib::Util::AsyncExecution<std::function<void(void)>> (std::forward<std::function<void(void)>>(funcClient));
    }

    AlgoLib::Util::TCleanup t([&pClientJob]{
        if(pClientJob != nullptr){
            delete pClientJob;
        }
    });

    while(true){
        std::unique_ptr<QueueElement> pElement = std::make_unique<QueueElement>();
        pRecvQueue->wait_dequeue(*pElement);

        dispatcher_round2(std::move(pElement));
    }
}

void ConnManager::dispatcher_test(std::unique_ptr<QueueElement> pElement){
    PeerConnection *pSender = pElement->upPeers->front();
    printf("Received message.\nFrom: BG %d SL %d\nType: %hu\n", pSender->m_upRemotePeer->m_BGid, pSender->m_upRemotePeer->m_SLid, pElement->pMessage->msgType);
}

void ConnManager::dispatcher_round2(std::unique_ptr<QueueElement> pElement){
    switch(pElement->pMessage->version){
        case VERSION_1_0:
        break;
        default:
        throw Exception(Exception::EXCEPTION_MESSAGE_INVALID_VERSION);
    }

    #ifdef DEBUG_PRINT
    PeerConnection *pSender = pElement->upPeers->front();
    if(pSender == nullptr){
        printf("BG %d SL %d received message.\nFrom: SELF\nType: %hu\n", m_upConfig->BGid, m_upConfig->SLid, pElement->pMessage->msgType);
    }
    else{
        printf("BG %d SL %d received message.\nFrom: BG %d SL %d\nType: %hu\n", m_upConfig->BGid, m_upConfig->SLid, pSender->m_upRemotePeer->m_BGid, pSender->m_upRemotePeer->m_SLid, pElement->pMessage->msgType);
    }
    
    #endif

    switch(pElement->pMessage->msgType){
        case MESSAGE_ROUND2_REQUEST:
        return dispatcher_round2_request(std::move(pElement));

        case MESSAGE_ROUND2_PREPREPARE:
        return dispatcher_round2_preprepare(std::move(pElement));

        case MESSAGE_ROUND2_PARTIAL_COMMIT:
        return dispatcher_round2_partialCommit(std::move(pElement));

        case MESSAGE_ROUND2_FULL_COMMIT:
        return dispatcher_round2_fullCommit(std::move(pElement));
    }
}

void ConnManager::leader_round2_sendPreprepare(){
    DebugThrow(canStartRound2());
    DebugThrow(m_upConfig->SLid == 0);

    if(pRound2_current_status != nullptr){
        throw Exception(Exception::EXCEPTION_UNEXPECTED);
    }

    pRound2_current_status = std::make_unique<CycleStatus>(m_upConfig->numBG());
    pRound2_current_status->state = CycleState::ROUND2_WAITING_FOR_PREPREPARE;
    pRound2_current_status->awaiting_message_type = MESSAGE_ROUND2_PREPREPARE;
    pRound2_current_status->message_received_counter = 0;
    pRound2_current_status->message_required = 1;

    int round3_participants_num = m_upConfig->numSL(m_upConfig->BGid) - m_upConfig->getF(m_upConfig->BGid);
    // Note: 2f+1 is fine if the BG has 3f+1 SLs, because gcd(2f+1, 3f+1) = 1
    // so the round robin way can iterate through all SLs.
    // However, if the BG contains 3f+2 or 3f+3 SLs, the round robin way may not work well.
    // An example would be 12 SLs with f=3. Only 4 SLs will become the first envoys.
    // The solution is to increase the number of envoys until it is coprime with the total number of SLs.
    // This does not affect the performance in no-failure cases because only the first envoy
    // will do the communication work.

    size_t size_round3_participants = sizeof(uint16_t) * round3_participants_num;
    size_t requestSize = m_upConfig->numSL(m_upConfig->BGid) * REQUEST_BATCH_SIZE;
    
    size_t payloadSize = size_round3_participants + requestSize;
    size_t totalSize = sizeof(MessageRound2Preprepare) + payloadSize;
    char *buffer = new char[totalSize];
    MessageRound2Preprepare *pPreprepare = (MessageRound2Preprepare *)buffer;

    pPreprepare->header.version = VERSION_LATEST;
    pPreprepare->header.msgType = MESSAGE_ROUND2_PREPREPARE;
    pPreprepare->header.payloadLen = payloadSize;
    pPreprepare->sender = m_upConfig->SLid;

    pPreprepare->view = round2_next_view;
    pPreprepare->seq = round2_next_seq;
    // Here we do not update the variables on the right hand side
    // because this Preprepare message will be delivered to the leader too
    // The leader will then check the view and seq and update them

    // No SkipCycle
    pPreprepare->lastcycle = leader_round2_next_rcanopus_cycle - 1;
    pPreprepare->cycle = leader_round2_next_rcanopus_cycle;
    leader_round2_next_rcanopus_cycle++;

    // Round-robin select collector excluding the leader
    pPreprepare->collector_SLid = leader_collector_rr;
    leader_collector_rr++;
    if(leader_collector_rr == m_upConfig->numSL(m_upConfig->BGid)){
        leader_collector_rr = 1u;    // never set to 0 because we don't want extra traffic through the leader
    }

    pPreprepare->numOfRound3Participants = round3_participants_num;
    uint16_t* pNextParticipant = (uint16_t *)pPreprepare->participantsAndContent;
    for(int i = 0; i < round3_participants_num; ++i, ++pNextParticipant){
        *pNextParticipant = leader_envoys_rr;
        leader_envoys_rr++;
        if(leader_envoys_rr == m_upConfig->numSL(m_upConfig->BGid)){
            leader_envoys_rr = 0u;   // May need to change if leader is overloaded
        }
    }

    char * contentBegin = pPreprepare->participantsAndContent + sizeof(uint16_t) * round3_participants_num;
    memset(contentBegin, pPreprepare->cycle & 0xFF, requestSize);

    QueueElement element;
    element.pMessage = (MessageHeader *)buffer;

    // Broadcast
    for(int sl = 0; sl < m_upConfig->numSL(m_upConfig->BGid); ++sl){
        if(sl == m_upConfig->SLid){
            // RHS is always 0 because we don't have view change
            continue;
        }

        element.upPeers->push_front(&rgrgConnection[m_upConfig->BGid][sl]);
    }

    // Don't forget to send a copy to myself
    element.upPeers->push_front(nullptr);
    
    pSendQueue->enqueue(std::move(element));
}

void ConnManager::dispatcher_round2_request(std::unique_ptr<QueueElement> pElement){
    if(!isRound2Leader()){
        DebugThrow(false);
        // Forward to leader
        PeerConnection *pLeaderConn = &rgrgConnection[m_upConfig->BGid][0];
        pSendQueue->enqueue(std::move(*pElement));
    }
    else{
        MessageRound2Request* pRequest = (MessageRound2Request *)pElement->pMessage;
        int numSL = m_upConfig->numSL(m_upConfig->BGid);
        DebugThrowElseReturnVoid(pRequest->sender < numSL);    // Invalid sender

        rgLeader_batch_received_from[pRequest->sender]++;

        if(canStartRound2()){
            #ifdef DEBUG_PRINT
            printf("Sending Preprepare message.\n");
            #endif
            leader_round2_sendPreprepare();
        }
    }
}

void ConnManager::dispatcher_round2_preprepare(std::unique_ptr<QueueElement> pElement){
    #ifdef DEBUG_PRINT
    printf("Dispatching Preprepare message.\n");
    #endif
    if(pRound2_current_status == nullptr){
        pRound2_current_status = std::make_unique<CycleStatus>(m_upConfig->numBG());
        pRound2_current_status->awaiting_message_type = MESSAGE_ROUND2_PREPREPARE;
        pRound2_current_status->message_received_counter = 0;
        pRound2_current_status->message_required = 1;
    }

    DebugThrowElseReturnVoid(pRound2_current_status->awaiting_message_type == MESSAGE_ROUND2_PREPREPARE);

    DebugThrow(pRound2_current_status->message_received_counter == 0);
    DebugThrow(pRound2_current_status->message_required == 1);

    // Neglected: verify the validity of the PrePrepare message

    MessageRound2Preprepare* pPreprepare = (MessageRound2Preprepare *)pElement->pMessage;
    DebugThrowElseReturnVoid(pPreprepare->view == round2_next_view);    // May need to change if allow ViewChange
    DebugThrowElseReturnVoid(pPreprepare->seq == round2_next_seq);  // May need to change if allow ViewChange

    pRound2_current_status->round2_sequence = pPreprepare->seq;
    pRound2_current_status->round2_view = pPreprepare->view;
    ++round2_next_seq;

    if(pPreprepare->collector_SLid == m_upConfig->SLid){
        // I'm selected as the collector for this cycle
        #ifdef DEBUG_PRINT
        printf("BG %d SL %d is the collector for cycle %hu\n", m_upConfig->BGid, m_upConfig->SLid, pPreprepare->cycle);
        #endif
        round2_isCollector = true;
        pRound2_current_status->state = CycleState::ROUND2_COLLECTOR_WAITING_FOR_PARTIALCOMMITS;
        pRound2_current_status->awaiting_message_type = MESSAGE_ROUND2_PARTIAL_COMMIT;
        pRound2_current_status->message_required = m_upConfig->numSL(m_upConfig->BGid);
        pRound2_current_status->message_received_counter = 0;
    }
    else{
        round2_isCollector = false;
        pRound2_current_status->state = CycleState::ROUND2_WAITING_FOR_FULLCOMMIT;
        pRound2_current_status->awaiting_message_type = MESSAGE_ROUND2_FULL_COMMIT;
        pRound2_current_status->message_required = 1;
        pRound2_current_status->message_received_counter = 0;
    }

    // Send partial commit message to collector
    // MESSAGE_ROUND2_PARTIAL_COMMIT
    char *buffer = new char[sizeof(MessageRound2PartialCommit)];
    MessageRound2PartialCommit *pPartialC = (MessageRound2PartialCommit *)buffer;

    pPartialC->header.version = VERSION_LATEST;
    pPartialC->header.msgType = MESSAGE_ROUND2_PARTIAL_COMMIT;
    pPartialC->header.payloadLen = sizeof(MessageRound2PartialCommit) - sizeof(MessageHeader);

    pPartialC->sender = m_upConfig->SLid;
    pPartialC->view = pRound2_current_status->round2_view;
    pPartialC->seq = pRound2_current_status->round2_sequence;

    memset(pPartialC->signature, 0, sizeof(pPartialC->signature));

    QueueElement element;
    element.pMessage = (MessageHeader *)buffer;

    if(pPreprepare->collector_SLid == m_upConfig->SLid){
        // Send to myself
        element.upPeers->push_front(nullptr);
    }
    else{
        element.upPeers->push_front(&rgrgConnection[m_upConfig->BGid][pPreprepare->collector_SLid]);
    }
    
    pSendQueue->enqueue(std::move(element));

    // Keep the Preprepare message in a temporary storage until it's committed
    DebugThrow(pTemporaryStorageOfPreprepare == nullptr);
    pTemporaryStorageOfPreprepare.swap(pElement);   // Keep the Preprepare message until it's committed
}

void ConnManager::dispatcher_round2_partialCommit(std::unique_ptr<QueueElement> pElement){
    // Queue this message again if it comes too early
    if(pRound2_current_status == nullptr || pRound2_current_status->state == CycleState::ROUND2_WAITING_FOR_PREPREPARE){
        // Don't let the unique_ptr delete this message
        QueueElement *ptr = pElement.release();
        this->pRecvQueue->enqueue(std::move(*ptr));

        sched_yield();
        return;
    }

    DebugThrowElseReturnVoid(round2_isCollector);
    DebugThrowElseReturnVoid(pRound2_current_status->state == CycleState::ROUND2_COLLECTOR_WAITING_FOR_PARTIALCOMMITS);

    MessageRound2PartialCommit* pPartialC = (MessageRound2PartialCommit *)pElement->pMessage;
    DebugThrowElseReturnVoid(pPartialC->view == pRound2_current_status->round2_view);
    DebugThrowElseReturnVoid(pPartialC->seq == pRound2_current_status->round2_sequence);

    if(round2_verifier == nullptr){
        throw Exception(Exception::EXCEPTION_VERIFIER_NOT_SET);
    }
    // Check signature
    DebugThrowElseReturnVoid(
        round2_verifier(
            (char *)pTemporaryStorageOfPreprepare->pMessage,
            pTemporaryStorageOfPreprepare->pMessage->payloadLen,
            pPartialC->signature,
            sizeof(pPartialC->signature)
            )
        );
    
    // increase counter
    
    // Neglected: We should check the message is from a sender we haven't seen in this cycle.
    // But we skip this check because we don't have any resending mechanisms.
    // Each message will be sent and received exactly once.
    pRound2_current_status->message_received_counter++;

    // Send FullCommit
    if(pRound2_current_status->message_received_counter == pRound2_current_status->message_required){
        char *buffer = new char[sizeof(MessageRound2FullCommit)];
        MessageRound2FullCommit *pFullC = (MessageRound2FullCommit *)buffer;

        pFullC->header.version = VERSION_LATEST;
        pFullC->header.msgType = MESSAGE_ROUND2_FULL_COMMIT;
        pFullC->header.payloadLen = sizeof(MessageRound2FullCommit) - sizeof(MessageHeader);

        pFullC->sender = m_upConfig->SLid;
        pFullC->view = pRound2_current_status->round2_view;
        pFullC->seq = pRound2_current_status->round2_sequence;

        memset(pFullC->combinedSignature, 0, sizeof(pFullC->combinedSignature));

        QueueElement element;
        element.pMessage = (MessageHeader *)buffer;

        // Broadcast
        for(int sl = 0; sl < m_upConfig->numSL(m_upConfig->BGid); ++sl){
            if(sl == m_upConfig->SLid){
                continue;
            }

            element.upPeers->push_front(&rgrgConnection[m_upConfig->BGid][sl]);
        }

        // Don't forget to send a copy to myself
        element.upPeers->push_front(nullptr);
        
        pSendQueue->enqueue(std::move(element));

        pRound2_current_status->awaiting_message_type = MESSAGE_ROUND2_FULL_COMMIT;
        pRound2_current_status->message_received_counter = 0;
        pRound2_current_status->message_required = 1;
        pRound2_current_status->state = CycleState::ROUND2_WAITING_FOR_FULLCOMMIT;
    }
}

void ConnManager::dispatcher_round2_fullCommit(std::unique_ptr<QueueElement> pElement){
    // Queue this message again if it comes too early
    if(pRound2_current_status == nullptr || pRound2_current_status->state == CycleState::ROUND2_WAITING_FOR_PREPREPARE){
        // Don't let the unique_ptr delete this message
        QueueElement *ptr = pElement.release();
        this->pRecvQueue->enqueue(std::move(*ptr));

        sched_yield();
        return;
    }

    DebugThrowElseReturnVoid(pRound2_current_status->state == CycleState::ROUND2_WAITING_FOR_FULLCOMMIT);

    MessageRound2FullCommit* pFullC = (MessageRound2FullCommit *)pElement->pMessage;
    DebugThrowElseReturnVoid(pFullC->view == pRound2_current_status->round2_view);
    DebugThrowElseReturnVoid(pFullC->seq == pRound2_current_status->round2_sequence);

    // Neglected: Should verify the combined signature contains a signature from everyone in this BG

    DebugThrowElseReturnVoid(pRound2_current_status->committedResult[m_upConfig->BGid] == nullptr);

    if(pTemporaryStorageOfPreprepare == nullptr){
        throw Exception(Exception::EXCEPTION_PREPREPARE_MISSING);
    }
    
    pRound2_current_status->committedResult[m_upConfig->BGid].swap(pTemporaryStorageOfPreprepare);

    // TODO: pass to round 3
    MessageRound2Preprepare *pPreprepare = (MessageRound2Preprepare *)pRound2_current_status->committedResult[m_upConfig->BGid]->pMessage;
    printf("Cycle %hu has committed in Round 2. Should start Round 3. Not implemented.\n", pPreprepare->cycle);
    pRound2_current_status = nullptr;
}

