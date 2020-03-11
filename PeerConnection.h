#ifndef PEER_CONNECTION_H_
#define PEER_CONNECTION_H_

#include "Peer.h"

class PeerConnection{
    private:
    std::unique_ptr<Peer> m_upRemotePeer;
    std::shared_ptr<Peer> m_spLocalPeer;
    public:
    PeerConnection();
    ~PeerConnection();
    bool reliableTransfer(char *src, size_t size);
};

#endif