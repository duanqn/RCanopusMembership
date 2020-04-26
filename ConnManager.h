#ifndef CONNMANAGER_H_
#define CONNMANAGER_H_

#include "config.h"
#include <memory>
#include "Peer.h"
#include "PeerConnection.h"
#include "const.h"
#include <map>
#include <sys/socket.h>
#include <cstring>
#include <chrono>
#include <unistd.h>
#include "util.h"
#include "message.h"
#include "blockingconcurrentqueue.h"
#include "QueueElement.h"
#include "poll.h"
#include <queue>
#include "CycleStatus.h"
#include <mutex>
#include <condition_variable>

using SigVerifier = bool (*)(char const *, size_t, char const *, size_t);

class ConnManager {
    private:
    std::unique_ptr<Config> m_upConfig;
    PeerConnection** rgrgConnection;

    std::chrono::milliseconds REQUEST_BATCH_INTERVAL;

    int numOfRemotePeers;
    struct pollfd *rgPoll;
    PeerConnection** rgPollSlotToConnection;

    moodycamel::BlockingConcurrentQueue<QueueElement> *pRecvQueue;
    moodycamel::BlockingConcurrentQueue<QueueElement> *pSendQueue;

    std::unordered_map<uint16_t, std::unique_ptr<CycleStatus>> mapRound3Status;
    std::map<uint16_t, std::chrono::time_point<std::chrono::steady_clock>> mapCycleSubmissionTime;

    int *rgLeader_batch_received_from;
    int leader_batch_received_from_self;
    int leader_send_to_self_pipe[2];
    int leader_batch_processed; // sent out preprepare
    uint16_t leader_round2_next_rcanopus_cycle;
    uint16_t round2_next_view;
    uint16_t round2_next_seq;
    std::unique_ptr<CycleStatus> pRound2_current_status;
    std::unique_ptr<QueueElement> pTemporaryStorageOfPreprepare;
    uint16_t leader_collector_rr;
    uint16_t leader_envoys_rr;
    bool round2_isCollector;
    SigVerifier round2_verifier;
    
    // Make sure you are not checking your own ID
    bool isPassiveConnection(int BGid, int SLid){
        if(BGid > m_upConfig->BGid){
            return true;
        }
        else if(BGid == m_upConfig->BGid){
            return SLid > m_upConfig->SLid;
        }
    }

    // Make sure you are not checking your own ID
    bool isActiveConnection(int BGid, int SLid){
        return !isPassiveConnection(BGid, SLid);
    }

    bool isRound2Leader(){
        // We never do view change!
        return m_upConfig->SLid == 0;
    }

    bool canStartRound2();
    bool isPrimaryEnvoyRound3(uint16_t cycle);
    bool isBackupEnvoyRound3(uint16_t cycle);

    int listenForIncomingConnections(int boundSocket);

    static char * recvMessage_caller_free_mem(int sock);
    void listener();
    void sender();
    void mockClient(std::chrono::milliseconds interval);
    void mockClientOnLeader(std::chrono::milliseconds interval);

    void leader_round2_sendPreprepare();
    void envoy_round3_sendFetchRequest(uint16_t cycle);
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
        delete pRecvQueue;
        delete pSendQueue;
        delete[] rgPollSlotToConnection;

        if(rgLeader_batch_received_from != nullptr){
            delete[] rgLeader_batch_received_from;
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

};
#endif
