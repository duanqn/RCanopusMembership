#include "ConnManager.h"
#include "const.h"
#include <sched.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <time.h>
#include <random>

bool cmpRequestType(MessageRound2Preprepare *px, MessageRound2Preprepare *py){
    return px->requestType < py->requestType;
}

char * ConnManager::recvMessage_caller_free_mem(int sock){
    char headerBuf[sizeof(MessageHeader)];
    PeerConnection::recv(sock, sizeof(MessageHeader), headerBuf);
    MessageHeader_BE *pbeHeader = (MessageHeader_BE *)headerBuf;
    MessageHeader *pHeader = MessageHeader_BE::deserialize(pbeHeader);

    size_t extraLen = pHeader->payloadLen;
    char* buffer = new char[sizeof(MessageHeader) + extraLen];
    #ifdef MEM_DBG
    heapalloc.fetch_add(sizeof(MessageHeader) + extraLen);
    #endif
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

        case MESSAGE_ROUND3_FETCH_REQUEST:
        MessageRound3FetchRequest_BE::partialDeserialize((MessageRound3FetchRequest_BE *)buffer);
        break;

        case MESSAGE_ROUND3_FETCH_RESPONSE:
        MessageRound3FetchResponse_BE::partialDeserialize((MessageRound3FetchResponse_BE *)buffer);
        break;

        case MESSAGE_ROUND3_GENERAL_FETCH:
        MessageRound3GeneralFetch_BE::partialDeserialize((MessageRound3GeneralFetch_BE *)buffer);
        break;

        case MESSAGE_ROUND3_CONNECTIVITY_RESPONSE:
        MessageRound3Connectivity_BE::partialDeserialize((MessageRound3Connectivity_BE *)buffer);
        break;

        case MESSAGE_ROUND3_MEMBERSHIP_RESPONSE:
        MessageRound3FullMembership_BE::partialDeserialize((MessageRound3FullMembership_BE *)buffer);
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

    AlgoLib::Util::TCleanup t([this, &boundSocket]{
        printf("BG %d SL %d closing the listening socket\n", this->m_upConfig->BGid, this->m_upConfig->SLid);
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

            #ifdef DEBUG_PRINT
            printf("BG %d SL %d received HELLO from BG %d SL %d, %d left\n", m_upConfig->BGid, m_upConfig->SLid, pHello->BGid, pHello->SLid, expectedConnections - i - 1);
            #endif
            rgrgConnection[pHello->BGid][pHello->SLid].fdSocket = newSocket;

            break;
        }while(true);
    }

    return 0;
}

// Constructor
ConnManager::ConnManager(const Config &conf):
    recvQueue(INIT_QUEUE_CAPACITY),
    sendQueue(INIT_QUEUE_CAPACITY),
    juggleQueue(),
    clientQueue(INIT_QUEUE_CAPACITY),
    REQUEST_BATCH_INTERVAL(0),
    numOfRemotePeers(0),
    m_upConfig(std::make_unique<Config>(conf)),
    rgrgConnection(nullptr),
    rgPoll(nullptr),
    rgPollSlotToConnection(nullptr),
    rgLeader_batch_received_from(nullptr),
    leader_batch_received_from_self(0),
    leader_batch_processed(0),
    pRound2_current_status(nullptr),
    leader_round2_next_rcanopus_cycle(1u),
    round2_next_view(1u),
    round2_next_seq(1u),
    m_device(),
    m_engine(m_device()),
    mapRound3Status(),
    mapRound3PendingFetchRequests(),
    mapRound3PendingConnectivityRequests(),
    mapRound3PendingMembershipRequests(),
    mapCycleSubmissionTime(),
    pTemporaryStorageOfPreprepare(nullptr),
    fakeRound3ResponseForBaseline(nullptr),
    leader_envoys_rr(0),
    leader_collector_rr(0),
    round2_verifier(nullptr),
    pStorage(std::make_unique<MockStorage>()),
    pLeaderRound2PendingPreprepareRaw(nullptr){

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

    if(isRound2Leader()){
        if(pipe2(leader_send_to_self_pipe, O_NONBLOCK) == -1){
            throw Exception(Exception::EXCEPTION_PIPE_CREATION);
        }
    }

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

        rgPoll[numOfRemotePeers].fd = leader_send_to_self_pipe[PIPE_READ];
        rgPoll[numOfRemotePeers].events = POLLIN;
        rgPoll[numOfRemotePeers].revents = 0;

        pLeaderRound2PendingPreprepareRaw = std::make_unique<std::priority_queue<MessageRound2Preprepare *, std::vector<MessageRound2Preprepare *>, bool(*)(MessageRound2Preprepare *, MessageRound2Preprepare *)>>(cmpRequestType);
    }
}

void ConnManager::listener(){
    int indexExit = numOfRemotePeers;
    int numfd = numOfRemotePeers;
    if(isRound2Leader()){
        // leader_send_to_self_pipe
        numfd++;
    }

    while(true){
        // TODO: listen for (numOfRemotePeers + 1) fds
        if(poll(rgPoll, numfd, -1) == -1){
            throw Exception(Exception::EXCEPTION_POLL);
        }

        // TODO: check rgPoll[indexExit] first

        for(int i = 0; i < numfd; ++i){
            if(rgPoll[i].revents != 0){
                if(rgPoll[i].revents & POLLIN != 0){
                    if(i == numOfRemotePeers){
                        // This is leader_send_to_self_pipe
                        bool finished = false;
                        do{
                            char buffer[PIPE_READ_BUFFER];
                            ssize_t readsize = read(leader_send_to_self_pipe[PIPE_READ], buffer, PIPE_READ_BUFFER);
                            if(readsize == -1 && (errno == EWOULDBLOCK || errno == EAGAIN)){
                                finished = true;
                            }
                            else if(readsize < PIPE_READ_BUFFER){
                                finished = true;
                            }
                            else{
                                throw Exception(Exception::EXCEPTION_RECV);
                            }
                        }while(!finished);

                        char *buffer = new char[sizeof(MessageEmpty)];
                        MessageEmpty *pEmpty = (MessageEmpty *)buffer;
                        pEmpty->header.version = VERSION_LATEST;
                        pEmpty->header.msgType = MESSAGE_EMPTY;
                        pEmpty->header.payloadLen = 0;

                        QueueElement element;
                        element.pMessage = (MessageHeader *)pEmpty;
                        element.upPeers->push_front(nullptr);
                        recvQueue.enqueue(std::move(element));
                    }
                    else{
                        // recv packet
                        char *pMessage = recvMessage_caller_free_mem(rgPoll[i].fd);
                        QueueElement element;
                        element.pMessage = (MessageHeader *)pMessage;
                        element.upPeers->push_front(rgPollSlotToConnection[i]);
                        recvQueue.enqueue(std::move(element));
                        
                        // Neglected: verify sender field (if the message has such a field)
                    }
                }
                else if(rgPoll[i].revents & POLLHUP != 0){
                    close(rgPoll[i].fd);
                    rgPoll[i].fd = -rgPoll[i].fd;
                }
                else{
                    DebugThrow(false);
                }

                rgPoll[i].revents = 0;
            }
        }
    }
}

void ConnManager::mockClient(std::chrono::milliseconds interval){
    int BGid = m_upConfig->BGid;
    PeerConnection *pLeader = &rgrgConnection[BGid][0];
    for(uint16_t cycle = 1u; ; cycle++){
        size_t size = sizeof(MessageRound2Request)+ REQUEST_BATCH_SIZE;
        char *buffer = new char[size];
        #ifdef MEM_DBG
        heapalloc.fetch_add(size);
        #endif
        MessageRound2Request *pRequest = (MessageRound2Request *)buffer;
        pRequest->header.version = VERSION_LATEST;
        pRequest->header.msgType = MESSAGE_ROUND2_REQUEST;
        pRequest->header.payloadLen = sizeof(MessageRound2Request)+ REQUEST_BATCH_SIZE - sizeof(MessageHeader);
        pRequest->sender = m_upConfig->SLid;

        QueueElement element;
        element.pMessage = (MessageHeader *)pRequest;
        element.upPeers->push_front(pLeader);
        
        clientQueue.enqueue(std::move(element));

        // Currently every cycle contains a request batch from every SL
        mapCycleSubmissionTime[cycle] = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(interval);
    }
}

void ConnManager::mockClientOnLeader(std::chrono::milliseconds interval){
    for(uint16_t cycle = 1u; ; cycle++){
        // Currently every cycle contains a request batch from every SL
        mapCycleSubmissionTime[cycle] = std::chrono::steady_clock::now();
        leader_batch_received_from_self++;

        if(write(leader_send_to_self_pipe[PIPE_WRITE], &cycle, sizeof(cycle)) == -1){
            throw Exception(Exception::EXCEPTION_SEND);
        }

        std::this_thread::sleep_for(interval);
    }
}

void ConnManager::sender(){
    while(true){
        // TODO: Check signal for exit

        QueueElement element;

        if(clientQueue.try_dequeue(element)){
            // send client messages first
        }
        else if(sendQueue.try_dequeue(element)){
        }
        else{
            continue;
        }

        size_t totalLength = sizeof(MessageHeader) + element.pMessage->payloadLen;

        for(auto it = element.upPeers->begin(); it != element.upPeers->end(); ++it){
            if(*it == nullptr){
                QueueElement copy;
                copy.clone(element);
                copy.upPeers->clear();
                copy.upPeers->push_front(nullptr);
                recvQueue.enqueue(std::move(copy));
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

            case MESSAGE_ROUND3_FETCH_REQUEST:
            MessageRound3FetchRequest::serialize((MessageRound3FetchRequest *)element.pMessage);
            break;

            case MESSAGE_ROUND3_FETCH_RESPONSE:
            MessageRound3FetchResponse::serialize((MessageRound3FetchResponse *)element.pMessage);
            break;

            case MESSAGE_ROUND3_GENERAL_FETCH:
            MessageRound3GeneralFetch::serialize((MessageRound3GeneralFetch *)element.pMessage);
            break;

            case MESSAGE_ROUND3_CONNECTIVITY_RESPONSE:
            MessageRound3Connectivity::serialize((MessageRound3Connectivity *)element.pMessage);
            break;

            case MESSAGE_ROUND3_MEMBERSHIP_RESPONSE:
            MessageRound3FullMembership::serialize((MessageRound3FullMembership *)element.pMessage);
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
        // Has pending preprepare messages
        if(!pLeaderRound2PendingPreprepareRaw->empty()){
            return true;
        }

        int SLnum = m_upConfig->numSL(m_upConfig->BGid);
        for(int i = 1; i < SLnum; ++i){
            // 0 is the leader itself
            if(rgLeader_batch_received_from[i] <= leader_batch_processed){
                // next batch not ready
                return false;
            }
        }

        if(leader_batch_received_from_self <= leader_batch_processed){
            return false;
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

    AlgoLib::Util::AsyncExecution<std::function<void(void)>> *pClientJob = nullptr;

    if(isRound2Leader()){
        std::function<void(void)> funcClient = std::bind(&ConnManager::mockClientOnLeader, this, REQUEST_BATCH_INTERVAL);
        pClientJob = new AlgoLib::Util::AsyncExecution<std::function<void(void)>> (std::forward<std::function<void(void)>>(funcClient));
    }
    else{
        std::function<void(void)> funcClient = std::bind(&ConnManager::mockClient, this, REQUEST_BATCH_INTERVAL);
        pClientJob = new AlgoLib::Util::AsyncExecution<std::function<void(void)>> (std::forward<std::function<void(void)>>(funcClient));
    }

    AlgoLib::Util::TCleanup t([&pClientJob]{
        if(pClientJob != nullptr){
            delete pClientJob;
        }
    });

    while(true){
        std::unique_ptr<QueueElement> pElement = std::make_unique<QueueElement>();
        recvQueue.wait_dequeue(*pElement);

        dispatcher_round2(std::move(pElement));
        size_t current_size = juggleQueue.size();

        // Process each message in juggle queue once (they may get juggled back)
        for(int i = 0; i < current_size; ++i){
            std::unique_ptr<QueueElement> pJuggleElement = std::make_unique<QueueElement>(std::move(juggleQueue.front()));
            juggleQueue.pop();

            dispatcher_round2(std::move(pJuggleElement));
        }
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
        case MESSAGE_EMPTY:
        DebugThrow(isRound2Leader());
        if(canStartRound2()){
            leader_round2_sendPreprepare();
        }
        return;
        
        case MESSAGE_ROUND2_REQUEST:
        return dispatcher_round2_request(std::move(pElement));

        case MESSAGE_ROUND2_PREPREPARE:
        return dispatcher_round2_preprepare(std::move(pElement));

        case MESSAGE_ROUND2_PARTIAL_COMMIT:
        return dispatcher_round2_partialCommit(std::move(pElement));

        case MESSAGE_ROUND2_FULL_COMMIT:
        return dispatcher_round2_fullCommit(std::move(pElement));

        case MESSAGE_ROUND3_FETCH_REQUEST:
        return dispatcher_round3_fetchRequest(std::move(pElement));

        case MESSAGE_ROUND3_FETCH_RESPONSE:
        return dispatcher_round3_fetchResponse(std::move(pElement));

        case MESSAGE_ROUND3_GENERAL_FETCH:
        return dispatcher_round3_generalFetchRequest(std::move(pElement));

        case MESSAGE_ROUND3_CONNECTIVITY_RESPONSE:
        return dispatcher_round3_fetchConnectivityResponse(std::move(pElement));

        case MESSAGE_ROUND3_MEMBERSHIP_RESPONSE:
        return dispatcher_round3_fetchMembershipResponse(std::move(pElement));

        default:
        throw Exception(Exception::EXCEPTION_MESSAGE_INVALID_TYPE);
    }
}

std::unique_ptr<std::forward_list<PeerConnection*> > ConnManager::getOneSLFromEveryRemoteBG(){
    std::unique_ptr<std::forward_list<PeerConnection*> > ret = std::make_unique<std::forward_list<PeerConnection*> >();
    for(int i = 0; i < m_upConfig->numBG(); ++i){
        if(i == m_upConfig->BGid){
            continue;
        }

        std::uniform_int_distribution<int> dist(0, m_upConfig->numSL(i) - 1);
        int recipient = dist(m_engine);
        ret->push_front(&rgrgConnection[i][recipient]);
    }

    return std::move(ret);
}

void ConnManager::leader_round2_sendPreprepare(){
    DebugThrow(m_upConfig->SLid == 0);
    DebugThrow(canStartRound2());

    if(pRound2_current_status != nullptr){
        throw Exception(Exception::EXCEPTION_UNEXPECTED);
    }

    // Set status
    pRound2_current_status = std::make_unique<CycleStatus>(m_upConfig->numBG());
    pRound2_current_status->state = CycleState::ROUND2_WAITING_FOR_PREPREPARE;
    pRound2_current_status->awaiting_message_type = MESSAGE_ROUND2_PREPREPARE;
    pRound2_current_status->message_received_counter = 0;
    pRound2_current_status->message_required = 1;

    MessageRound2Preprepare *pPreprepare = nullptr;
    
    if(!pLeaderRound2PendingPreprepareRaw->empty()){
        // Pop the Preprepare message from the priority queue
        pPreprepare = pLeaderRound2PendingPreprepareRaw->top();
        DebugThrow(pPreprepare->header.msgType == MESSAGE_ROUND2_PREPREPARE);
        DebugThrow(pPreprepare->numOfRound3Participants == 0);  // They cannot trigger Round 3
        pLeaderRound2PendingPreprepareRaw->pop();
    }
    else{
        // Create Preprepare Message
        #ifdef MEM_DBG
        printf("Message memory: %u bytes\n", heapalloc.load());
        #endif
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
        #ifdef MEM_DBG
        heapalloc.fetch_add(totalSize);
        #endif
        pPreprepare = (MessageRound2Preprepare *)buffer;

        pPreprepare->header.version = VERSION_LATEST;
        pPreprepare->header.msgType = MESSAGE_ROUND2_PREPREPARE;
        pPreprepare->header.payloadLen = payloadSize;
        pPreprepare->sender = m_upConfig->SLid;
        

        pPreprepare->BGid = (uint16_t)m_upConfig->BGid;
        pPreprepare->requestType = REQUEST_TYPE_FROM_CLIENT;
        // No SkipCycle
        pPreprepare->lastcycle = leader_round2_next_rcanopus_cycle - 1;
        pPreprepare->cycle = leader_round2_next_rcanopus_cycle;
        leader_round2_next_rcanopus_cycle++;
        leader_batch_processed++;

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
    }

    // common parts
    pPreprepare->view = round2_next_view;
    pPreprepare->seq = round2_next_seq;
    ++round2_next_seq;

    // Round-robin select collector excluding the leader
    pPreprepare->collector_SLid = leader_collector_rr;
    leader_collector_rr++;
    if(leader_collector_rr == m_upConfig->numSL(m_upConfig->BGid)){
        leader_collector_rr = 0u;
    }

    // Add to outbound queue
    QueueElement element;
    element.pMessage = (MessageHeader *)pPreprepare;

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
    
    sendQueue.enqueue(std::move(element));
}

void ConnManager::dispatcher_round2_request(std::unique_ptr<QueueElement> pElement){
    if(!isRound2Leader()){
        DebugThrow(false);
        // Forward to leader
        PeerConnection *pLeaderConn = &rgrgConnection[m_upConfig->BGid][0];
        sendQueue.enqueue(std::move(*pElement));
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
    if(pRound2_current_status == nullptr){
        pRound2_current_status = std::make_unique<CycleStatus>(m_upConfig->numBG());
        pRound2_current_status->awaiting_message_type = MESSAGE_ROUND2_PREPREPARE;
        pRound2_current_status->message_received_counter = 0;
        pRound2_current_status->message_required = 1;
    }

    if(pRound2_current_status->awaiting_message_type != MESSAGE_ROUND2_PREPREPARE){
        juggle(std::move(*pElement));
        return;
    }

    DebugThrow(pRound2_current_status->message_received_counter == 0);
    DebugThrow(pRound2_current_status->message_required == 1);

    // Neglected: verify the validity of the PrePrepare message

    MessageRound2Preprepare* pPreprepare = (MessageRound2Preprepare *)pElement->pMessage;
    if(pPreprepare->view > round2_next_view || (pPreprepare->view == round2_next_view && pPreprepare->seq > round2_next_seq)){
        juggle(std::move(*pElement));
        return;
    }

    DebugThrowElseReturnVoid(pPreprepare->view == round2_next_view);    // May need to change if allow ViewChange
    if(!isRound2Leader() && pPreprepare->seq != round2_next_seq){
        printf("BG %d SL %d expecting next seq %hu but got %hu!\n", m_upConfig->BGid, m_upConfig->SLid, round2_next_seq, pPreprepare->seq);
        DebugThrow(false);
    }  // May need to change if allow ViewChange


    pRound2_current_status->round2_sequence = pPreprepare->seq;
    pRound2_current_status->round2_view = pPreprepare->view;
    if(!isRound2Leader()){
        ++round2_next_seq;
    }

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
    #ifdef MEM_DBG
    heapalloc.fetch_add(sizeof(MessageRound2PartialCommit));
    #endif
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
    
    sendQueue.enqueue(std::move(element));

    // Keep the Preprepare message in a temporary storage until it's committed
    DebugThrow(pTemporaryStorageOfPreprepare == nullptr);
    pTemporaryStorageOfPreprepare.swap(pElement);   // Keep the Preprepare message until it's committed
}

void ConnManager::dispatcher_round2_partialCommit(std::unique_ptr<QueueElement> pElement){
    // Queue this message again if it comes too early
    if(pRound2_current_status == nullptr || pRound2_current_status->state == CycleState::ROUND2_WAITING_FOR_PREPREPARE){
        juggle(std::move(*pElement));
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
        #ifdef MEM_DBG
        heapalloc.fetch_add(sizeof(MessageRound2FullCommit));
        #endif
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
        
        sendQueue.enqueue(std::move(element));

        pRound2_current_status->awaiting_message_type = MESSAGE_ROUND2_FULL_COMMIT;
        pRound2_current_status->message_received_counter = 0;
        pRound2_current_status->message_required = 1;
        pRound2_current_status->state = CycleState::ROUND2_WAITING_FOR_FULLCOMMIT;
    }
}

void ConnManager::dispatcher_round2_fullCommit(std::unique_ptr<QueueElement> pElement){
    // Queue this message again if it comes too early
    if(pRound2_current_status == nullptr || pRound2_current_status->state == CycleState::ROUND2_WAITING_FOR_PREPREPARE){
        juggle(std::move(*pElement));
        return;
    }

    DebugThrowElseReturnVoid(pRound2_current_status->state == CycleState::ROUND2_WAITING_FOR_FULLCOMMIT);

    MessageRound2FullCommit* pFullC = (MessageRound2FullCommit *)pElement->pMessage;
    DebugThrowElseReturnVoid(pFullC->view == pRound2_current_status->round2_view);
    DebugThrowElseReturnVoid(pFullC->seq == pRound2_current_status->round2_sequence);

    // Neglected: Should verify the combined signature contains a signature from everyone in this BG


    if(pTemporaryStorageOfPreprepare == nullptr){
        throw Exception(Exception::EXCEPTION_PREPREPARE_MISSING);
    }
    
    pRound2_current_status->state = CycleState::ROUND2_COMMITTED;

    MessageRound2Preprepare *pPreprepare = (MessageRound2Preprepare *)pTemporaryStorageOfPreprepare->pMessage;

    #ifdef DEBUG_PRINT
    if(isRound2Leader()){
        printf("BG %d LEADER SEQ %hu Round 2 committed\n", m_upConfig->BGid, pPreprepare->seq);
    }

    AlgoLib::Util::TCleanup([]{
        fflush(stdout);
    });
    #endif
    
    if(pPreprepare->requestType == REQUEST_TYPE_FROM_CLIENT){
        for(uint16_t cycleNumber = pPreprepare->lastcycle + 1; cycleNumber < pPreprepare->cycle; ++cycleNumber){
            // Skipped cycles
            DebugThrow(false);  // We haven't implemented SkipCycle;

            // Forge a copy of the empty message
            size_t preprepareSize = getMessageSize((MessageHeader *)pPreprepare);
            size_t emptyMsgSize = sizeof(MessageRound2Preprepare) + pPreprepare->numOfRound3Participants * sizeof(uint16_t);
            char *buffer = new char[emptyMsgSize];
            AlgoLib::Util::TCleanup t([&buffer]{
                delete[] buffer;
            });
            memcpy(buffer, pPreprepare, emptyMsgSize);

            // Manually copy the CycleStatus
            MessageRound2Preprepare *pEmptyMsg = (MessageRound2Preprepare *)buffer;
            std::unique_ptr<CycleStatus> pMoveToRound3 = std::make_unique<CycleStatus>(m_upConfig->numBG());
            pMoveToRound3->round2_view = pRound2_current_status->round2_view;
            pMoveToRound3->round2_sequence = pRound2_current_status->round2_sequence;
            pMoveToRound3->awaiting_message_type = pRound2_current_status->awaiting_message_type;
            pMoveToRound3->state = CycleState::ROUND2_COMMITTED;
            pMoveToRound3->message_received_counter = pRound2_current_status->message_received_counter;
            pMoveToRound3->message_required = pRound2_current_status->message_required;

            MessageRound3FetchResponse *pFetchResponse = getRound3Response_caller_free_mem(pEmptyMsg, pFullC, m_upConfig->SLid, cycleNumber);

            std::unique_ptr<QueueElement> upElement = std::make_unique<QueueElement>();
            upElement->pMessage = (MessageHeader *)pFetchResponse;
            pMoveToRound3->rgMsgRound3FetchResponse[m_upConfig->BGid] = std::move(upElement);

            DebugThrow(mapRound3Status.find(cycleNumber) == mapRound3Status.end());
            mapRound3Status[cycleNumber] = std::move(pMoveToRound3);
            round3_respondToPendingFetchRequests(cycleNumber);
            envoy_round3_sendFetchRequest(cycleNumber);
        }

        uint16_t cycleNumber = pPreprepare->cycle;
        MessageRound3FetchResponse *pFetchResponse = getRound3Response_caller_free_mem(pPreprepare, pFullC, m_upConfig->SLid, cycleNumber);
        
        DebugThrowElseReturnVoid(pRound2_current_status->rgMsgRound3FetchResponse[m_upConfig->BGid] == nullptr);
        pRound2_current_status->rgMsgRound3FetchResponse[m_upConfig->BGid] = std::make_unique<QueueElement>();
        pRound2_current_status->rgMsgRound3FetchResponse[m_upConfig->BGid]->pMessage = (MessageHeader *)pFetchResponse;

        pTemporaryStorageOfPreprepare = nullptr;

        DebugThrow(mapRound3Status.find(cycleNumber) == mapRound3Status.end());
        mapRound3Status[cycleNumber] = std::move(pRound2_current_status);
        pRound2_current_status = nullptr;
        round3_respondToPendingFetchRequests(cycleNumber);
        envoy_round3_sendFetchRequest(cycleNumber);
    }
    else if(pPreprepare->requestType == REQUEST_TYPE_FETCHED_RESULT){
        uint16_t cycleNumber = pPreprepare->cycle;
        auto it = mapRound3Status.find(cycleNumber);
        if(it == mapRound3Status.end()){
            printf("Fetched result full commit: BG %d SL %d cannot find cycle %hu in Round 3 logs!\n", m_upConfig->BGid, m_upConfig->SLid, cycleNumber);
            throw Exception(Exception::EXCEPTION_CYCLE_NOT_IN_ROUND3);
        }

        // Some acrobat
        size_t remotePreprepareSize = pPreprepare->header.payloadLen + sizeof(MessageHeader) - sizeof(MessageRound2Preprepare) - SBFT_COMBINED_SIGNATURE_SIZE;
        MessageRound3FetchResponse *pFetchResponse = (MessageRound3FetchResponse *)new char[sizeof(MessageRound3FetchResponse) + remotePreprepareSize];
        #ifdef MEM_DBG
        heapalloc.fetch_add(sizeof(MessageRound3FetchResponse) + remotePreprepareSize);
        #endif
        pFetchResponse->header.version = VERSION_LATEST;
        pFetchResponse->header.msgType = MESSAGE_ROUND3_FETCH_RESPONSE;
        pFetchResponse->header.payloadLen = sizeof(MessageRound3FetchResponse) - sizeof(MessageHeader) + remotePreprepareSize;
        pFetchResponse->sender_BGid = pPreprepare->BGid;
        pFetchResponse->sender_SLid = INVALID_SENDER_ID;
        pFetchResponse->cycle = cycleNumber;
        memcpy(pFetchResponse->combinedSignature, pPreprepare->participantsAndContent, SBFT_COMBINED_SIGNATURE_SIZE);
        memcpy(pFetchResponse->entirePreprepareMsg, pPreprepare->participantsAndContent + SBFT_COMBINED_SIGNATURE_SIZE, remotePreprepareSize);

        DebugThrowElseReturnVoid(it->second->rgMsgRound3FetchResponse[pPreprepare->BGid] == nullptr);
        it->second->rgMsgRound3FetchResponse[pPreprepare->BGid] = std::make_unique<QueueElement>();
        it->second->rgMsgRound3FetchResponse[pPreprepare->BGid]->pMessage = (MessageHeader *)pFetchResponse;

        // These are no longer needed
        pTemporaryStorageOfPreprepare = nullptr;
        pRound2_current_status = nullptr;

        DebugThrow(it->second->awaiting_message_type == MESSAGE_ROUND3_FETCH_RESPONSE);
        it->second->message_received_counter++;
        if(it->second->message_received_counter == it->second->message_required){
            it->second->state = CycleState::ROUND3_WAITING_FOR_LOCAL_CONNECTIVITY;
            it->second->awaiting_message_type = MESSAGE_ROUND2_FULL_COMMIT;
            it->second->message_required = 1;
            it->second->message_received_counter = 0;

            if(isRound2Leader()){
                size_t connectivity_size = sizeof(uint16_t) + sizeof(char) * m_upConfig->numBG();
                char *buffer = new char[sizeof(MessageRound2Preprepare) + connectivity_size];
                #ifdef MEM_DBG
                heapalloc.fetch_add(sizeof(MESSAGE_ROUND2_PREPREPARE) + connectivity_size);
                #endif
                MessageRound2Preprepare *pNextPreprepare = (MessageRound2Preprepare *)buffer;
                pNextPreprepare->header.version = VERSION_LATEST;
                pNextPreprepare->header.msgType = MESSAGE_ROUND2_PREPREPARE;
                pNextPreprepare->header.payloadLen = sizeof(MessageRound2Preprepare) - sizeof(MessageHeader) + connectivity_size;

                pNextPreprepare->sender = m_upConfig->SLid; // always 0
                pNextPreprepare->view = 0;  // will be filled later
                pNextPreprepare->seq = 0;   // will be filled later
                pNextPreprepare->BGid = m_upConfig->BGid;
                pNextPreprepare->requestType = REQUEST_TYPE_LOCAL_CONNECTIVITY;
                pNextPreprepare->cycle = cycleNumber;
                pNextPreprepare->lastcycle = cycleNumber - 1;
                pNextPreprepare->collector_SLid = 0;    // will be filled later
                pNextPreprepare->numOfRound3Participants = 0;
                *(uint16_t *)pNextPreprepare->participantsAndContent = m_upConfig->numBG();
                char *pos = pNextPreprepare->participantsAndContent + sizeof(uint16_t);
                for(int i = 0; i < m_upConfig->numBG(); ++i){
                    if(it->second->rgMsgRound3FetchResponse[i] != nullptr){
                        *pos = 1;
                    }
                    else{
                        *pos = 0;
                    }
                    ++pos;
                }

                pLeaderRound2PendingPreprepareRaw->push(pNextPreprepare);
            }
        }
    }
    else if(pPreprepare->requestType == REQUEST_TYPE_LOCAL_CONNECTIVITY){
        uint16_t cycleNumber = pPreprepare->cycle;
        auto it = mapRound3Status.find(cycleNumber);
        if(it == mapRound3Status.end()){
            printf("Local connectivity full commit: BG %d SL %d cannot find cycle %hu in Round 3 logs!\n", m_upConfig->BGid, m_upConfig->SLid, cycleNumber);
            throw Exception(Exception::EXCEPTION_CYCLE_NOT_IN_ROUND3);
        }

        MessageRound3Connectivity *pConnectivityResponse = getRound3Response_caller_free_mem(pPreprepare, pFullC, m_upConfig->SLid, cycleNumber);
        pConnectivityResponse->header.msgType = MESSAGE_ROUND3_CONNECTIVITY_RESPONSE;
        
        DebugThrowElseReturnVoid(it->second->rgMsgRound3ConnectivityResponse[m_upConfig->BGid] == nullptr);
        it->second->rgMsgRound3ConnectivityResponse[m_upConfig->BGid] = std::make_unique<QueueElement>();
        it->second->rgMsgRound3ConnectivityResponse[m_upConfig->BGid]->pMessage = (MessageHeader *)pConnectivityResponse;
        it->second->state = CycleState::ROUND3_WAITING_FOR_REPLICATED_CONNECTIVITY;
        it->second->message_received_counter = 0;
        it->second->message_required = m_upConfig->numBG() - 1;
        it->second->awaiting_message_type = MESSAGE_ROUND3_CONNECTIVITY_RESPONSE;

        pTemporaryStorageOfPreprepare = nullptr;
        pRound2_current_status = nullptr;

        round3_respondToPendingFetchConnectivityRequests(cycleNumber);
        envoy_round3_sendFetchConnectivityRequest(cycleNumber);
    }
    else if(pPreprepare->requestType == REQUEST_TYPE_REMOTE_CONNECTIVITY){
        DebugThrow(false);  // this case has been eliminated
        uint16_t cycleNumber = pPreprepare->cycle;
        auto it = mapRound3Status.find(cycleNumber);
        if(it == mapRound3Status.end()){
            printf("Remote connectivity full commit: BG %d SL %d cannot find cycle %hu in Round 3 logs!\n", m_upConfig->BGid, m_upConfig->SLid, cycleNumber);
            throw Exception(Exception::EXCEPTION_CYCLE_NOT_IN_ROUND3);
        }

        // Some acrobat
        size_t remotePreprepareSize = pPreprepare->header.payloadLen + sizeof(MessageHeader) - sizeof(MessageRound2Preprepare) - SBFT_COMBINED_SIGNATURE_SIZE;
        MessageRound3Connectivity *pConnResponse = (MessageRound3Connectivity *)new char[sizeof(MessageRound3Connectivity) + remotePreprepareSize];
        #ifdef MEM_DBG
        heapalloc.fetch_add(sizeof(MessageRound3Connectivity) + remotePreprepareSize);
        #endif

        pConnResponse->header.version = VERSION_LATEST;
        pConnResponse->header.msgType = MESSAGE_ROUND3_CONNECTIVITY_RESPONSE;
        pConnResponse->header.payloadLen = remotePreprepareSize + sizeof(MessageRound3Connectivity) - sizeof(MessageHeader);

        pConnResponse->sender_BGid = pPreprepare->BGid;
        pConnResponse->sender_SLid = INVALID_SENDER_ID;
        pConnResponse->cycle = pPreprepare->cycle;

        memcpy(pConnResponse->combinedSignature, pPreprepare->participantsAndContent, SBFT_COMBINED_SIGNATURE_SIZE);
        memcpy(pConnResponse->entirePreprepareMsg, pPreprepare->participantsAndContent + SBFT_COMBINED_SIGNATURE_SIZE, remotePreprepareSize);

        DebugThrowElseReturnVoid(it->second->rgMsgRound3FetchResponse[pPreprepare->BGid] == nullptr);
        it->second->rgMsgRound3FetchResponse[pPreprepare->BGid] = std::make_unique<QueueElement>();
        it->second->rgMsgRound3FetchResponse[pPreprepare->BGid]->pMessage = (MessageHeader *)pConnResponse;

        // These are no longer needed
        pTemporaryStorageOfPreprepare = nullptr;
        pRound2_current_status = nullptr;

        DebugThrow(it->second->state == CycleState::ROUND3_WAITING_FOR_REPLICATED_CONNECTIVITY);
        DebugThrow(it->second->awaiting_message_type == MESSAGE_ROUND3_CONNECTIVITY_RESPONSE);
        it->second->message_received_counter++;

        if(it->second->message_received_counter == it->second->message_required){
            it->second->state = CycleState::ROUND3_WAITING_FOR_LOCAL_MEMBERSHIP;
            it->second->awaiting_message_type = MESSAGE_ROUND2_FULL_COMMIT;
            it->second->message_required = 1;
            it->second->message_received_counter = 0;

            if(isRound2Leader()){
                uint16_t BGnum = m_upConfig->numBG();
                size_t membershipSize = sizeof(uint16_t) + BGnum * BGnum * sizeof(char);
                
                char *buffer = new char[sizeof(MessageRound2Preprepare) + membershipSize];
                #ifdef MEM_DBG
                heapalloc.fetch_add(sizeof(MessageRound2Preprepare) + membershipSize);
                #endif
                MessageRound2Preprepare *pNextPreprepare = (MessageRound2Preprepare *)buffer;

                pNextPreprepare->header.version = VERSION_LATEST;
                pNextPreprepare->header.msgType = MESSAGE_ROUND2_PREPREPARE;
                pNextPreprepare->header.payloadLen = sizeof(MessageRound2Preprepare) + membershipSize - sizeof(MessageHeader);

                pNextPreprepare->sender = m_upConfig->SLid;
                pNextPreprepare->view = 0;  // to be filled later
                pNextPreprepare->seq = 0;   // to be filled later
                pNextPreprepare->BGid = m_upConfig->BGid;
                pNextPreprepare->requestType = REQUEST_TYPE_LOCAL_MEMBERSHIP;
                pNextPreprepare->cycle = cycleNumber;
                pNextPreprepare->lastcycle = cycleNumber - 1;
                pNextPreprepare->collector_SLid = 0;    // to be filled later;
                pNextPreprepare->numOfRound3Participants = 0;   // does not trigger round 3

            }
        }
    }
    else if(pPreprepare->requestType == REQUEST_TYPE_LOCAL_MEMBERSHIP){
        uint16_t cycleNumber = pPreprepare->cycle;
        auto it = mapRound3Status.find(cycleNumber);
        if(it == mapRound3Status.end()){
            printf("Local membership full commit: BG %d SL %d cannot find cycle %hu in Round 3 logs!\n", m_upConfig->BGid, m_upConfig->SLid, cycleNumber);
            throw Exception(Exception::EXCEPTION_CYCLE_NOT_IN_ROUND3);
        }

        size_t membershipSize = m_upConfig->numBG() * m_upConfig->numBG() * sizeof(bool);
        size_t totalSize = membershipSize + sizeof(MessageRound3FullMembership);

        DebugThrowElseReturnVoid(it->second->rgMsgRound3MembershipResponse[m_upConfig->BGid] == nullptr);
        it->second->rgMsgRound3MembershipResponse[m_upConfig->BGid] = std::make_unique<QueueElement>();
        it->second->rgMsgRound3MembershipResponse[m_upConfig->BGid]->pMessage = (MessageHeader *)new char[totalSize];
        #ifdef MEM_DBG
        heapalloc.fetch_add(totalSize);
        #endif

        MessageRound3FullMembership *pMembership = (MessageRound3FullMembership *)it->second->rgMsgRound3MembershipResponse[m_upConfig->BGid]->pMessage;
        pMembership->header.version = VERSION_LATEST;
        pMembership->header.msgType = MESSAGE_ROUND3_MEMBERSHIP_RESPONSE;
        pMembership->header.payloadLen = sizeof(MessageRound3FullMembership) - sizeof(MessageHeader) + membershipSize;

        pMembership->sender_BGid = m_upConfig->BGid;
        pMembership->sender_SLid = m_upConfig->SLid;
        pMembership->cycle = cycleNumber;
        pMembership->totalBGnum = m_upConfig->numBG();

        memset(pMembership->combinedSignature, 0, SBFT_COMBINED_SIGNATURE_SIZE);
        bool *pos = (bool *)pMembership->connectivity;
        for(int i = 0; i < m_upConfig->numBG(); ++i){
            for(int j = 0; j < m_upConfig->numBG(); ++j){
                *pos = true;    // TODO: handle failures
                pos++;
            }
        }

        it->second->state = CycleState::ROUND3_WAITING_FOR_REPLICATED_MEMBERSHIP;
        it->second->awaiting_message_type = MESSAGE_ROUND3_MEMBERSHIP_RESPONSE;
        it->second->message_received_counter = 0;
        it->second->message_required = m_upConfig->numBG() - 1;

        // These are no longer needed
        pTemporaryStorageOfPreprepare = nullptr;
        pRound2_current_status = nullptr;

        round3_respondToPendingFetchMembershipRequests(cycleNumber);
        envoy_round3_sendFetchMembershipRequest(cycleNumber);
    }
    else if(pPreprepare->requestType == REQUEST_TYPE_FULL_MEMBERSHIP){
        uint16_t cycleNumber = pPreprepare->cycle;
        auto it = mapRound3Status.find(cycleNumber);
        if(it == mapRound3Status.end()){
            printf("Full membership full commit: BG %d SL %d cannot find cycle %hu in Round 3 logs!\n", m_upConfig->BGid, m_upConfig->SLid, cycleNumber);
            throw Exception(Exception::EXCEPTION_CYCLE_NOT_IN_ROUND3);
        }

        // Manage status
        it->second->awaiting_message_type = MESSAGE_INVALID;
        it->second->message_received_counter = 0;
        it->second->message_required = 0;
        it->second->state = CycleState::ROUND3_COMMITTED;

        std::vector<int> votes((size_t)m_upConfig->numBG());
        for(int i = 0; i < votes.size(); ++i){
            votes[i] = 0;
        }

        char *pos = pPreprepare->participantsAndContent;
        char *end = (char *)pPreprepare + sizeof(MessageHeader) + pPreprepare->header.payloadLen;
        while(pos < end){
            MessageRound3FullMembership *pMembership = (MessageRound3FullMembership *)pos;
            DebugThrow(pMembership->totalBGnum == m_upConfig->numBG());
            for(int i = 0; i < pMembership->totalBGnum; ++i){
                for(int j = 0; j < pMembership->totalBGnum; ++j){
                    if(*(bool *)(&(pMembership->connectivity[i * pMembership->totalBGnum + j]))){
                        votes[j]++; // This is a vote from i to j
                    }
                    else{
                        DebugThrow(false);  // We don't have failures yet
                    }
                }
            }

            pos += sizeof(MessageHeader);
            pos += pMembership->header.payloadLen;
        }

        DebugThrow(pos == end); // The size calculation should be correct

        #ifdef DEBUG_PRINT
        printf("BG %d -- membership for cycle %hu:\n[", m_upConfig->BGid, cycleNumber);

        std::set<int> finalMembership;
        for(int i = 0; i < votes.size(); ++i){
            if(votes[i] >= m_upConfig->numBG() - m_upConfig->globalFailures){
                finalMembership.insert(i);
                printf("%d ", i);
            }
        }

        printf("]\n");
        #endif

        pTemporaryStorageOfPreprepare = nullptr;
        pRound2_current_status = nullptr;

        round3_committed(cycleNumber);
    }
    else{
        throw Exception(Exception::EXCEPTION_MESSAGE_INVALID_REQUEST_TYPE);
    }
    
    // Common: spawn new round 2 if possible
    if(m_upConfig->SLid == 0 && canStartRound2()){
        leader_round2_sendPreprepare();
    }
}

bool ConnManager::isPrimaryEnvoyRound3(uint16_t cycle){
    auto it = mapRound3Status.find(cycle);
    if(it == mapRound3Status.end()){
        printf("isPrimaryEnvoyRound3: BG %d SL %d cannot find cycle %hu in Round 3 logs!\n", m_upConfig->BGid, m_upConfig->SLid, cycle);
        throw Exception(Exception::EXCEPTION_CYCLE_NOT_IN_ROUND3);
    }

    MessageRound3FetchResponse *pFetchResponse = (MessageRound3FetchResponse *)(it->second->rgMsgRound3FetchResponse[m_upConfig->BGid]->pMessage);

    MessageRound2Preprepare *pPreprepare = (MessageRound2Preprepare *)pFetchResponse->entirePreprepareMsg;

    uint16_t numEnvoy = pPreprepare->numOfRound3Participants;
    uint16_t *pEnvoy = (uint16_t *)pPreprepare->participantsAndContent;
    if(numEnvoy == 0){
        throw Exception(Exception::EXCEPTION_NO_ENVOY_SELECTED);
    }

    return *pEnvoy == m_upConfig->SLid;
}

bool ConnManager::isBackupEnvoyRound3(uint16_t cycle){
    auto it = mapRound3Status.find(cycle);
    if(it == mapRound3Status.end()){
        printf("isBackupEnvoyRound3: BG %d SL %d cannot find cycle %hu in Round 3 logs!\n", m_upConfig->BGid, m_upConfig->SLid, cycle);
        throw Exception(Exception::EXCEPTION_CYCLE_NOT_IN_ROUND3);
    }

    MessageRound3FetchResponse *pFetchResponse = (MessageRound3FetchResponse *)(it->second->rgMsgRound3FetchResponse[m_upConfig->BGid]->pMessage);

    MessageRound2Preprepare *pPreprepare = (MessageRound2Preprepare *)pFetchResponse->entirePreprepareMsg;

    uint16_t numEnvoy = pPreprepare->numOfRound3Participants;
    uint16_t *pEnvoy = (uint16_t *)pPreprepare->participantsAndContent;
    if(numEnvoy == 0){
        throw Exception(Exception::EXCEPTION_NO_ENVOY_SELECTED);
    }
    else if(numEnvoy == 1){
        return false;
    }
    
    pEnvoy++;   // Skip the primary envoy
    for(int i = 1; i < numEnvoy; ++i){
        if(*pEnvoy == m_upConfig->SLid){
            return true;
        }

        pEnvoy++;
    }

    return false;
}

void ConnManager::round3_respondToPendingFetchRequests(uint16_t cycle){
    auto it = mapRound3Status.find(cycle);
    if(it == mapRound3Status.end()){
        printf("round3_respondToPendingFetchRequests: BG %d SL %d cannot find cycle %hu in Round 3 logs!\n", m_upConfig->BGid, m_upConfig->SLid, cycle);
        throw Exception(Exception::EXCEPTION_CYCLE_NOT_IN_ROUND3);
    }

    if(it->second->state != CycleState::ROUND2_COMMITTED){
        throw Exception(Exception::EXCEPTION_UNEXPECTED_CYCLE_STATE);
    }

    auto itPending = mapRound3PendingFetchRequests.find(cycle);
    if(itPending == mapRound3PendingFetchRequests.end()){
        // No previous pending fetch requests
        return;
    }

    QueueElement response;
    response.clone(*(it->second->rgMsgRound3FetchResponse[m_upConfig->BGid]));
    response.upPeers->clear();

    for(auto itRecp = itPending->second.begin(); itRecp != itPending->second.end(); ++itRecp){
        response.upPeers->push_front(&rgrgConnection[itRecp->first][itRecp->second]);
    }
    itPending->second.clear();

    sendQueue.enqueue(std::move(response));
}

// This function is executed by everyone but only the envoys will send out messages
void ConnManager::envoy_round3_sendFetchRequest(uint16_t cycle){
    auto it = mapRound3Status.find(cycle);
    if(it == mapRound3Status.end()){
        printf("envoy_round3_sendFetchRequest: BG %d SL %d cannot find cycle %hu in Round 3 logs!\n", m_upConfig->BGid, m_upConfig->SLid, cycle);
        throw Exception(Exception::EXCEPTION_CYCLE_NOT_IN_ROUND3);
    }

    if(it->second->state != CycleState::ROUND2_COMMITTED){
        throw Exception(Exception::EXCEPTION_UNEXPECTED_CYCLE_STATE);
    }

    if(m_upConfig->numBG() == 1){
        // There is no remote BG
        it->second->state = CycleState::ROUND3_COMMITTED;
        it->second->awaiting_message_type = MESSAGE_INVALID;
        it->second->message_required = 0;
        it->second->message_received_counter = 0;
        round3_committed(cycle);

        return;
    }

    it->second->state = CycleState::ROUND3_WAITING_FOR_REPLICATED_RESULTS;
    it->second->awaiting_message_type = MESSAGE_ROUND3_FETCH_RESPONSE;
    it->second->message_received_counter = 0;
    it->second->message_required = m_upConfig->numBG() - 1;

    if(isPrimaryEnvoyRound3(cycle)){
        MessageRound3FetchResponse *pFResp = (MessageRound3FetchResponse *)it->second->rgMsgRound3FetchResponse[m_upConfig->BGid]->pMessage;
        MessageRound3FetchRequest *pFReq = getRound3Request_caller_free_mem(pFResp);
        pFReq->sender_SLid = m_upConfig->SLid;

        QueueElement element;
        element.pMessage = (MessageHeader *)pFReq;
        element.upPeers = std::move(getOneSLFromEveryRemoteBG());
        sendQueue.enqueue(std::move(element));

        // Retry mechanism not implemented
        // TODO: After timeout (TIMEOUT_ENVOY), retry if the remote result has not been replicated
    }
    else if(isBackupEnvoyRound3(cycle)){
        // Do nothing
        // Retry mechanism not implemented
        // TODO: After timeout (TIMEOUT_ENVOY), retry if the remote result has not been replicated
    }
}

void ConnManager::round3_committed(uint16_t cycle){
    auto it = mapRound3Status.find(cycle);
    if(it == mapRound3Status.end()){
        printf("round3_committed: BG %d SL %d cannot find cycle %hu in Round 3 logs!\n", m_upConfig->BGid, m_upConfig->SLid, cycle);
        throw Exception(Exception::EXCEPTION_CYCLE_NOT_IN_ROUND3);
    }

    if(it->second->state != CycleState::ROUND3_COMMITTED){
        throw Exception(Exception::EXCEPTION_UNEXPECTED_CYCLE_STATE);
    }

    #ifdef DEBUG_PRINT
    printf("Cycle %hu committed in Round 3!\n", cycle);
    #endif

    // Erase the cycle status
    auto now = std::chrono::steady_clock::now();
    std::chrono::duration<double, std::milli> latency_ms = now - mapCycleSubmissionTime[cycle];

    struct timespec realtime;
    clock_gettime(CLOCK_REALTIME, &realtime);

    printf("!%ld.%09ld | Cycle %hu committed on BG %d SL %d | %lu | %lf | ms\n", realtime.tv_sec, realtime.tv_nsec, cycle, m_upConfig->BGid, m_upConfig->SLid, REQUEST_BATCH_SIZE / REQUEST_SIZE, latency_ms.count());
    fflush(stdout); // ensure we get all logs for finished cycles

    mapRound3Status.erase(it);
    mapCycleSubmissionTime.erase(cycle);
    pStorage->store(cycle);
}

void ConnManager::dispatcher_round3_fetchRequest(std::unique_ptr<QueueElement> pElement){
    MessageRound3FetchRequest *pFReq = (MessageRound3FetchRequest *)pElement->pMessage;
    uint16_t cycle = pFReq->cycle;

    auto it = mapRound3Status.find(cycle);
    if(it == mapRound3Status.end()){
        if(pStorage->exist(cycle)){
            // Stale request
            DebugThrowElseReturnVoid(false);
        }
        else{
            // Round 2 not finished yet
            #ifdef DEBUG_PRINT
            printf("BG %d SL %d received Round 3 fetch request from BG %hu SL %hu for cycle %hu but has not finished\n", m_upConfig->BGid, m_upConfig->SLid, pFReq->sender_BGid, pFReq->sender_SLid, cycle);
            #endif
            auto itPending = mapRound3PendingFetchRequests.find(cycle);
            if(itPending == mapRound3PendingFetchRequests.end()){
                mapRound3PendingFetchRequests[cycle] = std::vector<std::pair<uint16_t, uint16_t>>();
                itPending = mapRound3PendingFetchRequests.find(cycle);
            }

            itPending->second.push_back(std::make_pair(pFReq->sender_BGid, pFReq->sender_SLid));
        }
    }
    else{
        #ifdef DEBUG_PRINT
        printf("BG %d SL %d received Round 3 fetch request from BG %hu SL %hu for cycle %hu, writing a response now\n", m_upConfig->BGid, m_upConfig->SLid, pFReq->sender_BGid, pFReq->sender_SLid, cycle);
        #endif

        QueueElement response;
        response.clone(*(it->second->rgMsgRound3FetchResponse[m_upConfig->BGid]));
        response.upPeers->clear();

        response.upPeers->push_front(&rgrgConnection[pFReq->sender_BGid][pFReq->sender_SLid]);

        sendQueue.enqueue(std::move(response));
    }
}

void ConnManager::dispatcher_round3_fetchResponse(std::unique_ptr<QueueElement> pElement){
    // Neglected: Should check the signature in MessageRound3FetchResponse

    if(!isRound2Leader()){
        pElement->upPeers->clear();
        pElement->upPeers->push_front(&rgrgConnection[m_upConfig->BGid][0]);
        sendQueue.enqueue(std::move(*pElement));

        return;
    }
    // Round 2 leader

    MessageRound3FetchResponse *pFResp = (MessageRound3FetchResponse *)pElement->pMessage;
    
    size_t remotePreprepareSize = pFResp->header.payloadLen - (sizeof(MessageRound3FetchResponse) - sizeof(MessageHeader));
    size_t localMessageSize = remotePreprepareSize + sizeof(pFResp->combinedSignature);
    size_t totalSize = sizeof(MessageRound2Preprepare) + localMessageSize;

    char *buffer = new char[totalSize];
    #ifdef MEM_DBG
    heapalloc.fetch_add(totalSize);
    #endif

    MessageRound2Preprepare *pPreprepare = (MessageRound2Preprepare *)buffer;

    pPreprepare->header.version = VERSION_LATEST;
    pPreprepare->header.msgType = MESSAGE_ROUND2_PREPREPARE;
    pPreprepare->header.payloadLen = totalSize - sizeof(MessageHeader);

    pPreprepare->sender = m_upConfig->SLid;    // Leader is always 0
    pPreprepare->view = 0;  // to be filled later
    pPreprepare->seq = 0;   // to be filled later

    pPreprepare->BGid = pFResp->sender_BGid;
    pPreprepare->requestType = REQUEST_TYPE_FETCHED_RESULT;

    // SkipCycle is transparent in Round 3
    pPreprepare->cycle = pFResp->cycle;
    pPreprepare->lastcycle = pFResp->cycle - 1;

    pPreprepare->collector_SLid = 0;    // to be filled later

    pPreprepare->numOfRound3Participants = 0;   // This will not trigger Round 3
    char *pos = pPreprepare->participantsAndContent;
    memcpy(pos, pFResp->combinedSignature, SBFT_COMBINED_SIGNATURE_SIZE);
    pos += SBFT_COMBINED_SIGNATURE_SIZE;
    memcpy(pos, pFResp->entirePreprepareMsg, remotePreprepareSize);

    pLeaderRound2PendingPreprepareRaw->push(pPreprepare);

    if(canStartRound2()){
        leader_round2_sendPreprepare();
    }
}

// This function is executed by everyone but only the envoys will send out messages
void ConnManager::envoy_round3_sendFetchConnectivityRequest(uint16_t cycle){
    auto it = mapRound3Status.find(cycle);
    if(it == mapRound3Status.end()){
        printf("envoy_round3_sendFetchConnectivityRequest: BG %d SL %d cannot find cycle %hu in Round 3 logs!\n", m_upConfig->BGid, m_upConfig->SLid, cycle);
        throw Exception(Exception::EXCEPTION_CYCLE_NOT_IN_ROUND3);
    }

    DebugThrow(m_upConfig->numBG() > 1);

    if(isPrimaryEnvoyRound3(cycle)){
        char *buffer = new char[sizeof(MessageRound3GeneralFetch)];
        #ifdef MEM_DBG
        heapalloc.fetch_add(sizeof(MessageRound3GeneralFetch));
        #endif
        MessageRound3GeneralFetch *pFetch = (MessageRound3GeneralFetch *)buffer;
        pFetch->header.version = VERSION_LATEST;
        pFetch->header.payloadLen = sizeof(MessageRound3GeneralFetch) - sizeof(MessageHeader);
        pFetch->header.msgType = MESSAGE_ROUND3_GENERAL_FETCH;
        pFetch->sender_BGid = m_upConfig->BGid;
        pFetch->sender_SLid = m_upConfig->SLid;
        pFetch->cycle = cycle;
        pFetch->msgTypeDemanded = MESSAGE_ROUND3_CONNECTIVITY_RESPONSE;

        QueueElement element;
        element.upPeers = std::move(getOneSLFromEveryRemoteBG());
        element.pMessage = (MessageHeader *)pFetch;
        sendQueue.enqueue(std::move(element));        
    }
    else if(isBackupEnvoyRound3(cycle)){
        // Not implemented
    }
}

void ConnManager::round3_respondToPendingFetchConnectivityRequests(uint16_t cycle){
    auto it = mapRound3Status.find(cycle);
    if(it == mapRound3Status.end()){
        printf("round3_respondToPendingFetchConnectivityRequests: BG %d SL %d cannot find cycle %hu in Round 3 logs!\n", m_upConfig->BGid, m_upConfig->SLid, cycle);
        throw Exception(Exception::EXCEPTION_CYCLE_NOT_IN_ROUND3);
    }

    auto itPending = mapRound3PendingConnectivityRequests.find(cycle);
    if(itPending == mapRound3PendingConnectivityRequests.end()){
        // No previous pending fetch requests
        return;
    }

    #ifdef DEBUG_PRINT
    printf("BG %d SL %d is now ready to respond to fetch connectivity request for cycle %hu\n", m_upConfig->BGid, m_upConfig->SLid, cycle);
    #endif

    QueueElement response;
    response.clone(*(it->second->rgMsgRound3ConnectivityResponse[m_upConfig->BGid]));
    response.upPeers->clear();

    for(auto itRecp = itPending->second.begin(); itRecp != itPending->second.end(); ++itRecp){
        response.upPeers->push_front(&rgrgConnection[itRecp->first][itRecp->second]);
    }
    itPending->second.clear();

    sendQueue.enqueue(std::move(response));
}

void ConnManager::dispatcher_round3_fetchConnectivityRequest(std::unique_ptr<QueueElement> pElement){
    MessageRound3GeneralFetch *pFetch = (MessageRound3GeneralFetch *)pElement->pMessage;
    uint16_t cycle = pFetch->cycle;

    auto it = mapRound3Status.find(cycle);
    if(it == mapRound3Status.end()){
        printf("dispatcher_round3_fetchConnectivityRequest: BG %d SL %d cannot find cycle %hu in Round 3 logs!\n", m_upConfig->BGid, m_upConfig->SLid, cycle);
        juggle(std::move(*pElement));
        return;
    }

    if(it->second->rgMsgRound3ConnectivityResponse[m_upConfig->BGid] == nullptr){
        #ifdef DEBUG_PRINT
        printf("BG %d SL %d received fetch connectivity request for cycle %hu but is not ready\n", m_upConfig->BGid, m_upConfig->SLid, cycle);
        #endif
        // not ready yet
        auto itPending = mapRound3PendingConnectivityRequests.find(cycle);
        if(itPending == mapRound3PendingConnectivityRequests.end()){
            mapRound3PendingConnectivityRequests[cycle] = std::vector<std::pair<uint16_t, uint16_t>>();
            itPending = mapRound3PendingConnectivityRequests.find(cycle);
        }

        itPending->second.push_back(std::make_pair(pFetch->sender_BGid, pFetch->sender_SLid));
    }
    else{
        #ifdef DEBUG_PRINT
        printf("BG %d SL %d received fetch connectivity request for cycle %hu and is ready\n", m_upConfig->BGid, m_upConfig->SLid, cycle);
        #endif
        QueueElement response;
        response.clone(*(it->second->rgMsgRound3ConnectivityResponse[m_upConfig->BGid]));
        response.upPeers->clear();

        response.upPeers->push_front(&rgrgConnection[pFetch->sender_BGid][pFetch->sender_SLid]);

        sendQueue.enqueue(std::move(response));
    }
}

void ConnManager::dispatcher_round3_fetchConnectivityResponse(std::unique_ptr<QueueElement> pElement){
    if(!isRound2Leader()){
        pElement->upPeers->clear();
        pElement->upPeers->push_front(&rgrgConnection[m_upConfig->BGid][0]);
        sendQueue.enqueue(std::move(*pElement));

        return;
    }

    MessageRound3Connectivity *pConn = (MessageRound3Connectivity *)pElement->pMessage;
    auto it = mapRound3Status.find(pConn->cycle);
    if(it == mapRound3Status.end()){
        printf("dispatcher_round3_fetchConnectivityResponse: BG %d SL %d cannot find cycle %hu in Round 3 logs!\n", m_upConfig->BGid, m_upConfig->SLid, pConn->cycle);
        throw Exception(Exception::EXCEPTION_CYCLE_NOT_IN_ROUND3);
    }

    it->second->rgMsgRound3ConnectivityResponse[pConn->sender_BGid] = std::move(pElement);

    if(it->second->awaiting_message_type != MESSAGE_ROUND3_CONNECTIVITY_RESPONSE){
        printf("BG %hu SL %hu got connectivity response from BG %hu SL %hu for cycle %hu, but I never asked for it!\n", m_upConfig->BGid, m_upConfig->SLid, pConn->sender_BGid, pConn->sender_SLid, pConn->cycle);
    }

    it->second->message_received_counter++;
    if(it->second->message_received_counter == it->second->message_required){
        // TODO: also triggered by timer
        size_t totalLocalMsgSize = 0;
        for(auto itResp = it->second->rgMsgRound3ConnectivityResponse.begin(); itResp != it->second->rgMsgRound3ConnectivityResponse.end(); ++itResp){
            size_t remotePreprepareSize = pConn->header.payloadLen - (sizeof(MessageRound3Connectivity) + sizeof(MessageHeader));
            totalLocalMsgSize += remotePreprepareSize + sizeof(pConn->combinedSignature);
        }
        size_t totalSize = sizeof(MessageRound2Preprepare) + totalLocalMsgSize;

        char *buffer = new char[totalSize];
        #ifdef MEM_DBG
        heapalloc.fetch_add(totalSize);
        #endif

        MessageRound2Preprepare *pPreprepare = (MessageRound2Preprepare *)buffer;

        pPreprepare->header.version = VERSION_LATEST;
        pPreprepare->header.msgType = MESSAGE_ROUND2_PREPREPARE;
        pPreprepare->header.payloadLen = totalSize - sizeof(MessageHeader);

        pPreprepare->sender = m_upConfig->SLid;
        pPreprepare->view = 0;  // to be filled
        pPreprepare->seq = 0;   // to be filled;
        pPreprepare->BGid = pConn->sender_BGid;
        pPreprepare->requestType = REQUEST_TYPE_LOCAL_MEMBERSHIP;
        pPreprepare->cycle = pConn->cycle;
        pPreprepare->lastcycle = pConn->cycle - 1;
        pPreprepare->collector_SLid = 0;    // to be filled;
        pPreprepare->numOfRound3Participants = 0;

        char *pos = pPreprepare->participantsAndContent;
        memset(pos, 0, totalLocalMsgSize);

        pLeaderRound2PendingPreprepareRaw->push(pPreprepare);
    }

    if(canStartRound2()){
        leader_round2_sendPreprepare();
    }
}

void ConnManager::dispatcher_round3_fetchMembershipRequest(std::unique_ptr<QueueElement> pElement){
    MessageRound3GeneralFetch *pFetch = (MessageRound3GeneralFetch *)pElement->pMessage;
    uint16_t cycle = pFetch->cycle;

    auto it = mapRound3Status.find(cycle);
    if(it == mapRound3Status.end()){
        // Round already completed
        DebugThrow(pStorage->exist(cycle));

        MessageRound3FullMembership *pFake = (MessageRound3FullMembership *)new char[sizeof(MessageRound3FullMembership) + m_upConfig->numBG() * m_upConfig->numBG() * sizeof(bool)];
        #ifdef MEM_DBG
        heapalloc.fetch_add(sizeof(MessageRound3FullMembership) + m_upConfig->numBG() * m_upConfig->numBG() * sizeof(bool));
        #endif

        pFake->header.version = VERSION_LATEST;
        pFake->header.msgType = MESSAGE_ROUND3_MEMBERSHIP_RESPONSE;
        pFake->header.payloadLen = sizeof(MessageRound3FullMembership) + m_upConfig->numBG() * m_upConfig->numBG() * sizeof(bool) - sizeof(MessageHeader);

        pFake->cycle = cycle;
        pFake->sender_BGid = m_upConfig->BGid;
        pFake->sender_SLid = m_upConfig->SLid;
        pFake->totalBGnum = m_upConfig->numBG();
        memset(pFake->combinedSignature, 0, SBFT_COMBINED_SIGNATURE_SIZE);
        memset(pFake->connectivity, 1, m_upConfig->numBG() * m_upConfig->numBG());

        QueueElement element;
        element.pMessage = (MessageHeader *)pFake;
        element.upPeers->push_front(&rgrgConnection[pFetch->sender_BGid][pFetch->sender_SLid]);

        sendQueue.enqueue(std::move(element));

        return;
    }

    if(it->second->rgMsgRound3MembershipResponse[m_upConfig->BGid] == nullptr){
        #ifdef DEBUG_PRINT
        printf("BG %d SL %d received fetch membership request for cycle %hu but is not ready\n", m_upConfig->BGid, m_upConfig->SLid, cycle);
        #endif
        // not ready yet
        auto itPending = mapRound3PendingMembershipRequests.find(cycle);
        if(itPending == mapRound3PendingMembershipRequests.end()){
            mapRound3PendingMembershipRequests[cycle] = std::vector<std::pair<uint16_t, uint16_t>>();
            itPending = mapRound3PendingMembershipRequests.find(cycle);
        }

        itPending->second.push_back(std::make_pair(pFetch->sender_BGid, pFetch->sender_SLid));
    }
    else{
        #ifdef DEBUG_PRINT
        printf("BG %d SL %d received fetch membership request for cycle %hu and is ready\n", m_upConfig->BGid, m_upConfig->SLid, cycle);
        #endif
        QueueElement response;
        response.clone(*(it->second->rgMsgRound3MembershipResponse[m_upConfig->BGid]));
        response.upPeers->clear();

        response.upPeers->push_front(&rgrgConnection[pFetch->sender_BGid][pFetch->sender_SLid]);

        sendQueue.enqueue(std::move(response));
    }
}

void ConnManager::dispatcher_round3_fetchMembershipResponse(std::unique_ptr<QueueElement> pElement){
    if(!isRound2Leader()){
        pElement->upPeers->clear();
        pElement->upPeers->push_front(&rgrgConnection[m_upConfig->BGid][0]);
        sendQueue.enqueue(std::move(*pElement));

        return;
    }

    MessageRound3FullMembership *pMembership = (MessageRound3FullMembership *)pElement->pMessage;
    uint16_t cycle = pMembership->cycle;

    auto it = mapRound3Status.find(cycle);
    if(it == mapRound3Status.end()){
        printf("dispatcher_round3_fetchMembershipResponse: BG %d SL %d cannot find cycle %hu in Round 3 logs!\n", m_upConfig->BGid, m_upConfig->SLid, cycle);
        throw Exception(Exception::EXCEPTION_CYCLE_NOT_IN_ROUND3);
    }

    if(it->second->state != CycleState::ROUND3_WAITING_FOR_REPLICATED_MEMBERSHIP){
        printf("BG %d SL %d cycle %hu got membership response but is still in state %d!\n", m_upConfig->BGid, m_upConfig->SLid, cycle, int(it->second->state));
        juggle(std::move(*pElement));
        return;
    }
    DebugThrowElseReturnVoid(it->second->rgMsgRound3MembershipResponse[pMembership->sender_BGid] == nullptr);
    DebugThrow(it->second->awaiting_message_type == MESSAGE_ROUND3_MEMBERSHIP_RESPONSE);
    

    pElement->upPeers->clear();
    it->second->rgMsgRound3MembershipResponse[pMembership->sender_BGid] = std::move(pElement);
    it->second->message_received_counter++;

    #ifdef DEBUG_PRINT
    printf("BG %d got %d remote membership responses, %d left\n", m_upConfig->BGid, it->second->message_received_counter, it->second->message_required - it->second->message_received_counter);
    #endif

    if(it->second->message_received_counter == it->second->message_required){
        // TODO: also triggered by timeout
        size_t remoteMsgSize = 0;
        for(auto itMembership = it->second->rgMsgRound3MembershipResponse.begin(); itMembership != it->second->rgMsgRound3MembershipResponse.end(); ++itMembership){
            if(*itMembership == nullptr){
                continue;
            }
            
            // Also replicate the membership from this local BG
            remoteMsgSize += sizeof(MessageHeader) + (*itMembership)->pMessage->payloadLen;
        }

        size_t totalSize = sizeof(MessageRound2Preprepare) + remoteMsgSize;
        MessageRound2Preprepare *pPreprepare = (MessageRound2Preprepare *)new char[totalSize];
        #ifdef MEM_DBG
        heapalloc.fetch_add(totalSize);
        #endif

        pPreprepare->header.version = VERSION_LATEST;
        pPreprepare->header.msgType = MESSAGE_ROUND2_PREPREPARE;
        pPreprepare->header.payloadLen = totalSize - sizeof(MessageHeader);

        pPreprepare->sender = m_upConfig->SLid;
        pPreprepare->view = 0;  // will be filled
        pPreprepare->seq = 0;   // will be filled
        pPreprepare->BGid = m_upConfig->BGid;
        pPreprepare->requestType = REQUEST_TYPE_FULL_MEMBERSHIP;
        pPreprepare->cycle = cycle;
        pPreprepare->lastcycle = cycle - 1;
        pPreprepare->collector_SLid = 0;    // will be filled
        pPreprepare->numOfRound3Participants = 0;   // does not trigger Round 3

        char *pos = pPreprepare->participantsAndContent;
        for(auto itMembership = it->second->rgMsgRound3MembershipResponse.begin(); itMembership != it->second->rgMsgRound3MembershipResponse.end(); ++itMembership){
            if(*itMembership == nullptr){
                continue;
            }
            
            size_t msgSize = sizeof(MessageHeader) + (*itMembership)->pMessage->payloadLen;
            memcpy(pos, (*itMembership)->pMessage, msgSize);
            pos += msgSize;
        }

        pLeaderRound2PendingPreprepareRaw->push(pPreprepare);

        if(canStartRound2()){
            leader_round2_sendPreprepare();
        }
    }
}

void ConnManager::dispatcher_round3_generalFetchRequest(std::unique_ptr<QueueElement> pElement){
    MessageRound3GeneralFetch *pFetch = (MessageRound3GeneralFetch *)pElement->pMessage;
    switch(pFetch->msgTypeDemanded){
        case MESSAGE_ROUND3_CONNECTIVITY_RESPONSE:
        return dispatcher_round3_fetchConnectivityRequest(std::move(pElement));

        case MESSAGE_ROUND3_MEMBERSHIP_RESPONSE:
        return dispatcher_round3_fetchMembershipRequest(std::move(pElement));

        default:
        throw Exception(Exception::EXCEPTION_MESSAGE_INVALID_TYPE);
    }
}

void ConnManager::envoy_round3_sendFetchMembershipRequest(uint16_t cycle){
    auto it = mapRound3Status.find(cycle);
    if(it == mapRound3Status.end()){
        printf("envoy_round3_sendFetchMembershipRequest: BG %d SL %d cannot find cycle %hu in Round 3 logs!\n", m_upConfig->BGid, m_upConfig->SLid, cycle);
        throw Exception(Exception::EXCEPTION_CYCLE_NOT_IN_ROUND3);
    }

    DebugThrow(m_upConfig->numBG() > 1);

    if(isPrimaryEnvoyRound3(cycle)){
        char *buffer = new char[sizeof(MessageRound3GeneralFetch)];
        #ifdef MEM_DBG
        heapalloc.fetch_add(sizeof(MessageRound3GeneralFetch));
        #endif
        MessageRound3GeneralFetch *pFetch = (MessageRound3GeneralFetch *)buffer;
        pFetch->header.version = VERSION_LATEST;
        pFetch->header.payloadLen = sizeof(MessageRound3GeneralFetch) - sizeof(MessageHeader);
        pFetch->header.msgType = MESSAGE_ROUND3_GENERAL_FETCH;
        pFetch->sender_BGid = m_upConfig->BGid;
        pFetch->sender_SLid = m_upConfig->SLid;
        pFetch->cycle = cycle;
        pFetch->msgTypeDemanded = MESSAGE_ROUND3_MEMBERSHIP_RESPONSE;

        QueueElement element;
        element.upPeers = std::move(getOneSLFromEveryRemoteBG());
        element.pMessage = (MessageHeader *)pFetch;
        sendQueue.enqueue(std::move(element));        
    }
    else if(isBackupEnvoyRound3(cycle)){
        // Not implemented
    }
}

void ConnManager::round3_respondToPendingFetchMembershipRequests(uint16_t cycle){
    auto it = mapRound3Status.find(cycle);
    if(it == mapRound3Status.end()){
        printf("round3_respondToPendingFetchMembershipRequests: BG %d SL %d cannot find cycle %hu in Round 3 logs!\n", m_upConfig->BGid, m_upConfig->SLid, cycle);
        throw Exception(Exception::EXCEPTION_CYCLE_NOT_IN_ROUND3);
    }

    auto itPending = mapRound3PendingMembershipRequests.find(cycle);
    if(itPending == mapRound3PendingMembershipRequests.end()){
        // No previous pending fetch requests
        return;
    }

    #ifdef DEBUG_PRINT
    printf("BG %d SL %d is now ready to respond to fetch membership request for cycle %hu\n", m_upConfig->BGid, m_upConfig->SLid, cycle);
    #endif

    QueueElement response;
    response.clone(*(it->second->rgMsgRound3MembershipResponse[m_upConfig->BGid]));
    response.upPeers->clear();

    for(auto itRecp = itPending->second.begin(); itRecp != itPending->second.end(); ++itRecp){
        response.upPeers->push_front(&rgrgConnection[itRecp->first][itRecp->second]);
    }
    itPending->second.clear();

    sendQueue.enqueue(std::move(response));
}

void ConnManager::dispatcher_round3_preprepare(std::unique_ptr<QueueElement> pElement){
    MessageRound3PreprepareBaseline *pPreprepare3 = (MessageRound3PreprepareBaseline *)pElement->pMessage;
    uint16_t cycle = pPreprepare3->cycle;

    auto it = mapRound3Status.find(cycle);
    if(it == mapRound3Status.end()){
        printf("dispatcher_round3_preprepare: BG %d SL %d cannot find cycle %hu in Round 3 logs!\n", m_upConfig->BGid, m_upConfig->SLid, cycle);
        throw Exception(Exception::EXCEPTION_CYCLE_NOT_IN_ROUND3);
    }

    if(it->second->state != CycleState::ROUND3_WAITING_FOR_PREPREPARE_BASELINE){
        juggle(std::move(*pElement));
        return;
    }

    DebugThrowElseReturnVoid(it->second->awaiting_message_type == MESSAGE_ROUND3_PREPREPARE_BASELINE);
    DebugThrowElseReturnVoid(it->second->message_received_counter == 0);

    if(pPreprepare3->collector_BGid == m_upConfig->BGid){
        // I'm the collector
        #ifdef DEBUG_PRINT
        printf("BG %d SL %d is the Round 3 collector for cycle %hu\n", m_upConfig->BGid, m_upConfig->SLid, pPreprepare3->cycle);
        #endif
        it->second->state = CycleState::ROUND3_COLLECTOR_WAITING_FOR_PARTIALCOMMITS_BASELINE;
        it->second->awaiting_message_type = MESSAGE_ROUND3_PARTIAL_COMMIT_BASELINE;
        it->second->message_received_counter = 0;
        it->second->message_required = m_upConfig->numBG();
    }
    else{
        it->second->state = CycleState::ROUND3_WAITING_FOR_FULLCOMMIT_BASELINE;
        it->second->awaiting_message_type = MESSAGE_ROUND3_FULL_COMMIT_BASELINE;
        it->second->message_received_counter = 0;
        it->second->message_required = 1;
    }

    char *buffer = new char[sizeof(MessageRound3PartialCommitBaseline)];
    MessageRound3PartialCommitBaseline *pPartialC = (MessageRound3PartialCommitBaseline *)buffer;
    pPartialC->header.version = VERSION_LATEST;
    pPartialC->header.msgType = MESSAGE_ROUND3_PARTIAL_COMMIT_BASELINE;
    pPartialC->header.payloadLen = sizeof(MessageRound3PartialCommitBaseline) - sizeof(MessageHeader);

    pPartialC->cycle = cycle;
    pPartialC->sender = m_upConfig->BGid;
    memset(pPartialC->signature, 0, sizeof(pPartialC->signature));

    QueueElement element;
    element.pMessage = (MessageHeader *)pPartialC;

    if(pPreprepare3->collector_BGid == m_upConfig->BGid){
        // I'm the collector, send to myself
        element.upPeers->push_front(nullptr);
    }
    else{
        element.upPeers->push_front(&rgrgConnection[pPreprepare3->collector_BGid][0]);
    }

    sendQueue.enqueue(std::move(element));
}

void ConnManager::dispatcher_round3_partialCommit(std::unique_ptr<QueueElement> pElement){
    MessageRound3PreprepareBaseline *pPreprepare3 = (MessageRound3PreprepareBaseline *)pElement->pMessage;
    uint16_t cycle = pPreprepare3->cycle;

    auto it = mapRound3Status.find(cycle);
    if(it == mapRound3Status.end()){
        printf("dispatcher_round3_preprepare: BG %d SL %d cannot find cycle %hu in Round 3 logs!\n", m_upConfig->BGid, m_upConfig->SLid, cycle);
        throw Exception(Exception::EXCEPTION_CYCLE_NOT_IN_ROUND3);
    }

    if(it->second->state != CycleState::ROUND3_COLLECTOR_WAITING_FOR_PARTIALCOMMITS_BASELINE){
        juggle(std::move(*pElement));
        return;
    }

    DebugThrowElseReturnVoid(it->second->awaiting_message_type == MESSAGE_ROUND3_PARTIAL_COMMIT_BASELINE);
    
    it->second->message_received_counter++;

    if(it->second->message_received_counter == it->second->message_required){
        it->second->state = CycleState::ROUND3_WAITING_FOR_FULLCOMMIT_BASELINE;
        it->second->awaiting_message_type = MESSAGE_ROUND3_FULL_COMMIT_BASELINE;
        it->second->message_received_counter = 0;
        it->second->message_required = 1;

        // Send full commit
        char *buffer = new char[sizeof(MessageRound3FullCommitBaseline)];
        MessageRound3FullCommitBaseline *pFullC = (MessageRound3FullCommitBaseline *)buffer;
        pFullC->header.version = VERSION_LATEST;
        pFullC->header.msgType = MESSAGE_ROUND3_FULL_COMMIT_BASELINE;
        
        pFullC->sender = m_upConfig->BGid;
        pFullC->cycle = cycle;
        memset(pFullC->combinedSignature, 0, sizeof(pFullC->combinedSignature));

        QueueElement element;
        element.pMessage = (MessageHeader *)pFullC;
        for(int i = 0; i < m_upConfig->numBG(); ++i){
            if(i == m_upConfig->BGid){
                continue;
            }
            element.upPeers->push_front(&rgrgConnection[i][0]);
        }
        element.upPeers->push_front(nullptr);
    }
}

void ConnManager::dispatcher_round3_fullCommit(std::unique_ptr<QueueElement> pElement){
    MessageRound3PreprepareBaseline *pPreprepare3 = (MessageRound3PreprepareBaseline *)pElement->pMessage;
    uint16_t cycle = pPreprepare3->cycle;

    auto it = mapRound3Status.find(cycle);
    if(it == mapRound3Status.end()){
        printf("dispatcher_round3_preprepare: BG %d SL %d cannot find cycle %hu in Round 3 logs!\n", m_upConfig->BGid, m_upConfig->SLid, cycle);
        throw Exception(Exception::EXCEPTION_CYCLE_NOT_IN_ROUND3);
    }

    if(it->second->state != CycleState::ROUND3_WAITING_FOR_FULLCOMMIT_BASELINE){
        juggle(std::move(*pElement));
        return;
    }

    it->second->state = CycleState::ROUND3_COMMITTED;
    it->second->awaiting_message_type = MESSAGE_INVALID;
    it->second->message_received_counter = 0;
    it->second->message_required = 0;

    round3_committed(cycle);
}
