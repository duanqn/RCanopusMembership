#ifndef PEER_H_
#define PEER_H_
#include <cstdint>
#include <memory>
#include <netinet/ip.h>

class Peer{
    private:
    uint32_t m_id;
    std::unique_ptr<struct sockaddr_in> m_upNetworkAddress;

    public:
    std::unique_ptr<struct sockaddr_in> getCopyOfAddress();
    
};

#endif