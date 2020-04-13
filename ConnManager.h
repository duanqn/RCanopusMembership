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
#include "readerwriterqueue.h"
#include "blockingconcurrentqueue.h"
#include "QueueElement.h"
#include "poll.h"

class ConnManager final {
    private:
    std::unique_ptr<Config> m_upConfig;
    PeerConnection** rgrgConnection;

    int numOfRemotePeers;
    struct pollfd *rgPoll;
    PeerConnection** rgPollSlotToConnection;

    moodycamel::BlockingReaderWriterQueue<QueueElement> *pRecvQueue;
    moodycamel::BlockingConcurrentQueue<QueueElement> *pSendQueue;

    // Make sure you are not checking your own ID
    bool isPassiveConnection(int BGid, int SLid){
        if(BGid > m_upConfig->BGid){
            return true;
        }
        else if(BGid == m_upConfig->BGid){
            return SLid > m_upConfig->SLid;
        }
    }

    bool isActiveConnection(int BGid, int SLid){
        return !isPassiveConnection(BGid, SLid);
    }

    int listenForIncomingConnections(int boundSocket);

    static char * recvMessage_caller_free_mem(int sock);
    void listener();
    void sender();
    void mockClient(std::chrono::milliseconds interval);

    public:
    ConnManager(const Config &conf);

    ~ConnManager(){
        int BGnum = m_upConfig->rgrgPeerAddr.size();
        for(int i = 0; i < BGnum; ++i){
            delete[] rgrgConnection[i];
        }
        delete[] rgrgConnection;

        delete[] rgPoll;
        delete pRecvQueue;
        delete pSendQueue;
        delete[] rgPollSlotToConnection;
    }

    void start();

    void dispatcher_test();

};
#endif