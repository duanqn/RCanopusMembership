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
#include <unistd.h>
#include "util.h"

class ConnManager final {
    private:
    std::unique_ptr<Config> m_upConfig;
    PeerConnection** rgrgConnection;

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

    int listenForIncomingConnections(int boundSocket){
        if(listen(boundSocket, LISTEN_BACKLOG) == -1){
            return -1;
        }

        int expectedConnections = 0;
        int BGnum = m_upConfig->rgrgPeerAddr.size();
        for(int bg = 0; bg < BGnum; ++bg){
            for(int sl = 0; sl < m_upConfig->rgrgPeerAddr[bg].size(); ++sl){
                if(isPassiveConnection(bg, sl)){
                    ++expectedConnections;
                }
            }
        }

        struct sockaddr_in remoteAddr;
        socklen_t remoteAddrLen = sizeof(remoteAddr);
        for(int i = 0; i < expectedConnections;){
            int newSocket = accept(boundSocket, (sockaddr *)&remoteAddr, &remoteAddrLen);
            if(newSocket == -1){
                continue;
            }

            try{
                // Loop 0
                for(int bg = 0; bg < BGnum; ++bg){
                    // Loop 1
                    for(int sl = 0; sl < m_upConfig->rgrgPeerAddr[bg].size(); ++sl){
                        if(memcmp(&remoteAddr, &(rgrgConnection[bg][sl].m_upRemotePeer->m_addr), remoteAddrLen) == 0){
                            rgrgConnection[bg][sl].fdSocket = newSocket;
                            throw FastBreak(0);
                        }
                    }
                }

                // Close the socket if there is no match
                printf("No match for incoming connection!\n");
                close(newSocket);
            }
            catch(FastBreak &e){
                // Only count this connection if we can find a 
                ++i;
            }
        }

        return 0;
    }

    public:
    ConnManager(const Config &conf){
        m_upConfig = std::make_unique<Config>(conf);

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
        AlgoLib::Util::AsyncExecution jobListen(std::move(funcBind), std::move(socketListen));

        // Build connection table
        for(int bg = 0; bg < BGnum; ++bg){
            for(int sl = 0; sl < m_upConfig->rgrgPeerAddr[bg].size(); ++sl){
                if(bg == m_upConfig->BGid && sl == m_upConfig->SLid){
                    continue;
                }

                // Construct remote peer
                std::unique_ptr<Peer> upPeer = std::make_unique<Peer>(bg, sl);
                upPeer->m_addr = m_upConfig->rgrgPeerAddr[bg][sl];
                rgrgConnection[bg][sl].m_upRemotePeer = std::move(upPeer);
                rgrgConnection[bg][sl].m_spLocalPeer = spLocal;
            }
        }

        // TODO: Dial to build those active connections

        jobListen.waitForResult();
    }

    ~ConnManager(){
        int BGnum = m_upConfig->rgrgPeerAddr.size();
        for(int i = 0; i < BGnum; ++i){
            delete[] rgrgConnection[i];
        }
        delete[] rgrgConnection;
    }
};
#endif