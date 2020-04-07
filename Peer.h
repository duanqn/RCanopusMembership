#ifndef PEER_H_
#define PEER_H_
#include <cstdint>
#include <memory>
#include <netinet/ip.h>

class Peer{
    public:
    int m_BGid;
    int m_SLid;
    struct sockaddr_in m_addr;
    
    Peer(int BG, int SL):
        m_BGid(BG),
        m_SLid(SL)
    {}
};

#endif