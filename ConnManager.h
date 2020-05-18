#ifndef CONNMANAGER_H_
#define CONNMANAGER_H_

#include "config.h"
#include <memory>
#include "Peer.h"
#include "PeerConnection.h"
#include "const.h"
#include <map>
#include <unordered_map>
#include <sys/socket.h>
#include <cstring>
#include <chrono>
#include <unistd.h>
#include "util.h"
#include "message.h"
#include "blockingconcurrentqueue.h"
#include "readerwriterqueue.h"
#include "QueueElement.h"
#include "poll.h"
#include <queue>
#include "CycleStatus.h"
#include <mutex>
#include <condition_variable>
#include "MockStorage.h"
#include <random>

using SigVerifier = bool (*)(char const *, size_t, char const *, size_t);

class ConnManager {
    private:
    std::unique_ptr<Config> m_upConfig;
    PeerConnection** rgrgConnection;

    std::chrono::milliseconds REQUEST_BATCH_INTERVAL;

    int numOfRemotePeers;
    struct pollfd *rgPoll;
    PeerConnection** rgPollSlotToConnection;

    moodycamel::BlockingConcurrentQueue<QueueElement> recvQueue;
    std::queue<QueueElement> juggleQueue;   // sync access only
    moodycamel::BlockingReaderWriterQueue<QueueElement> sendQueue;
    moodycamel::BlockingReaderWriterQueue<QueueElement> clientQueue;
    
    std::random_device m_device;
    std::default_random_engine m_engine;

    std::unordered_map<uint16_t, std::unique_ptr<CycleStatus>> mapRound3Status;
    std::map<uint16_t, std::vector<std::pair<uint16_t, uint16_t>>> mapRound3PendingFetchRequests;
    std::map<uint16_t, std::vector<std::pair<uint16_t, uint16_t>>> mapRound3PendingConnectivityRequests;
    std::map<uint16_t, std::vector<std::pair<uint16_t, uint16_t>>> mapRound3PendingMembershipRequests;
    std::map<uint16_t, std::chrono::time_point<std::chrono::steady_clock>> mapCycleSubmissionTime;

    char *fakeRound3ResponseForBaseline;

    int *rgLeader_batch_received_from;
    int leader_batch_received_from_self;
    int leader_send_to_self_pipe[2];
    int leader_batch_processed; // sent out preprepare
    uint16_t leader_round2_next_rcanopus_cycle;
    uint16_t round2_next_view;
    uint16_t round2_next_seq;
    std::unique_ptr<CycleStatus> pRound2_current_status;
    std::unique_ptr<QueueElement> pTemporaryStorageOfPreprepare;
    std::unique_ptr<std::priority_queue<MessageRound2Preprepare *, std::vector<MessageRound2Preprepare *>, bool(*)(MessageRound2Preprepare *, MessageRound2Preprepare *)>> pLeaderRound2PendingPreprepareRaw;   // priority_queue does not support move-only types
    uint16_t leader_collector_rr;
    uint16_t leader_envoys_rr;
    bool round2_isCollector;
    SigVerifier round2_verifier;
    std::unique_ptr<MockStorage> pStorage;
    
    // Make sure you are not checking your own ID
    bool isPassiveConnection(int BGid, int SLid){
        if(BGid > m_upConfig->BGid){
            return true;
        }
        else if(BGid == m_upConfig->BGid){
            return SLid > m_upConfig->SLid;
        }
        return false;
    }

    // Make sure you are not checking your own ID
    bool isActiveConnection(int BGid, int SLid){
        return !isPassiveConnection(BGid, SLid);
    }

    inline bool isRound2Leader(){
        // We never do view change!
        return m_upConfig->SLid == 0;
    }

    inline bool isGlobalLeader(){
        return m_upConfig->BGid == 0 && m_upConfig->SLid == 0;
    }

    inline void juggle(QueueElement element){
        juggleQueue.push(std::move(element));
    }

    bool canStartRound2();
    bool isPrimaryEnvoyRound3(uint16_t cycle);
    bool isBackupEnvoyRound3(uint16_t cycle);
    std::unique_ptr<std::forward_list<PeerConnection*> > getOneSLFromEveryRemoteBG();

    int listenForIncomingConnections(int boundSocket);

    static char * recvMessage_caller_free_mem(int sock);
    void listener();
    void sender();
    void mockClient(std::chrono::milliseconds interval);
    void mockClientOnLeader(std::chrono::milliseconds interval);

    void leader_round2_sendPreprepare();
    void envoy_round3_sendFetchRequest(uint16_t cycle);
    void envoy_round3_sendFetchConnectivityRequest(uint16_t cycle);
    void envoy_round3_sendFetchMembershipRequest(uint16_t cycle);
    void round3_respondToPendingFetchRequests(uint16_t cycle);
    void round3_respondToPendingFetchConnectivityRequests(uint16_t cycle);
    void round3_respondToPendingFetchMembershipRequests(uint16_t cycle);
    void round3_committed(uint16_t cycle);

    public:
    ConnManager(const Config &conf);

    virtual ~ConnManager(){
        int BGnum = m_upConfig->numBG();
        for(int i = 0; i < BGnum; ++i){
            delete[] rgrgConnection[i];
        }
        delete[] rgrgConnection;

        delete[] rgPoll;
        delete[] rgPollSlotToConnection;

        if(rgLeader_batch_received_from != nullptr){
            delete[] rgLeader_batch_received_from;
        }

        if(fakeRound3ResponseForBaseline != nullptr){
            delete[] fakeRound3ResponseForBaseline;
        }
    }

    void start();
    inline void setVerifier(SigVerifier v){
        round2_verifier = v;
    }

    private:
    void dispatcher_test(std::unique_ptr<QueueElement> pElement);

    virtual void dispatcher_round2(std::unique_ptr<QueueElement> pElement);
    virtual void dispatcher_round2_request(std::unique_ptr<QueueElement> pElement);
    virtual void dispatcher_round2_preprepare(std::unique_ptr<QueueElement> pElement);
    virtual void dispatcher_round2_partialCommit(std::unique_ptr<QueueElement> pElement);
    virtual void dispatcher_round2_fullCommit(std::unique_ptr<QueueElement> pElement);
    virtual void dispatcher_round3_fetchRequest(std::unique_ptr<QueueElement> pElement);
    virtual void dispatcher_round3_fetchResponse(std::unique_ptr<QueueElement> pElement);
    virtual void dispatcher_round3_generalFetchRequest(std::unique_ptr<QueueElement> pElement);
    virtual void dispatcher_round3_fetchConnectivityRequest(std::unique_ptr<QueueElement> pElement);
    virtual void dispatcher_round3_fetchConnectivityResponse(std::unique_ptr<QueueElement> pElement);
    virtual void dispatcher_round3_fetchMembershipRequest(std::unique_ptr<QueueElement> pElement);
    virtual void dispatcher_round3_fetchMembershipResponse(std::unique_ptr<QueueElement> pElement);
    virtual void dispatcher_round3_preprepare(std::unique_ptr<QueueElement> pElement);
    virtual void dispatcher_round3_partialCommit(std::unique_ptr<QueueElement> pElement);
    virtual void dispatcher_round3_fullCommit(std::unique_ptr<QueueElement> pElement);
};

#endif
