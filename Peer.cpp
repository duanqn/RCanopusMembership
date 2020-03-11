#include "Peer.h"
#include <cstring>

std::unique_ptr<struct sockaddr_in> Peer::getCopyOfAddress(){
    using return_type = std::unique_ptr<struct sockaddr_in>;

    struct sockaddr_in *pAddr = new struct sockaddr_in;
    if(m_upNetworkAddress == nullptr){
        return return_type(nullptr);
    }

    memcpy(pAddr, m_upNetworkAddress.get(), sizeof(struct sockaddr_in));

    return return_type(nullptr);
}