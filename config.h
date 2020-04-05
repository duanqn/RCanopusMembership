#ifndef CONFIG_H_
#define CONFIG_H_

#include <cstdio>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <vector>
struct Config{
    int nMembershipParticipants;
    std::vector<struct sockaddr_in> rgPeerAddr;
    Config(): rgPeerAddr(){
        nMembershipParticipants = 0;
    }
    Config(Config&& c)
        :nMembershipParticipants(c.nMembershipParticipants),
        rgPeerAddr(std::move(c.rgPeerAddr))
    {}
};

struct Config parseFromFile(FILE *f);

#endif