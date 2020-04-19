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

class ConnManager {
    private:
    std::unique_ptr<Config> m_upConfig;
    PeerConnection** rgrgConnection;

    int numOfRemotePeers;
    struct pollfd *rgPoll;
    PeerConnection** rgPollSlotToConnection;

    moodycamel::BlockingConcurrentQueue<QueueElement> *pRecvQueue;
    moodycamel::BlockingConcurrentQueue<QueueElement> *pSendQueue;

    std::unordered_map<int, CycleStatus> mapRound3Status;

    int *rgLeader_batch_received_from;
    int leader_batch_processed; // sent out preprepare
    uint16_t leader_round2_next_rcanopus_cycle;
    uint16_t round2_next_view;
    uint16_t round2_next_seq;
    std::unique_ptr<CycleStatus> pRound2_current_status;
    std::unique_ptr<QueueElement> pTemporaryStorageOfPreprepare;
    uint16_t leader_collector_rr;
    uint16_t leader_envoys_rr;
    bool round2_isCollector;
    
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

    int listenForIncomingConnections(int boundSocket);

    static char * recvMessage_caller_free_mem(int sock);
    void listener();
    void sender();
    void mockClient(std::chrono::milliseconds interval);

    void leader_round2_sendPreprepare();

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

    private:
    void dispatcher_test(std::unique_ptr<QueueElement> pElement);

    virtual void dispatcher_round2(std::unique_ptr<QueueElement> pElement);
    virtual void dispatcher_round2_request(std::unique_ptr<QueueElement> pElement);
    virtual void dispatcher_round2_preprepare(std::unique_ptr<QueueElement> pElement);
    virtual void dispatcher_round2_partialCommit(std::unique_ptr<QueueElement> pElement);
    virtual void dispatcher_round2_fullCommit(std::unique_ptr<QueueElement> pElement);

};
#endif
