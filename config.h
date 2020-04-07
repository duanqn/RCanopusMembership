#ifndef CONFIG_H_
#define CONFIG_H_

#include <cstdio>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <map>
struct Config{
    int nMembershipParticipants;
    std::map<std::pair<int, int>, struct sockaddr_in> mapSuperLeafAddr;
    int BGid;
    int SLid;

    Config(): mapSuperLeafAddr(){
        nMembershipParticipants = 0;
        BGid = SLid = 0;
    }

    Config(Config&& c)
        :nMembershipParticipants(c.nMembershipParticipants),
        mapSuperLeafAddr(std::move(c.mapSuperLeafAddr)),
        BGid(c.BGid),
        SLid(c.SLid)
    {}

    Config(const Config& c)
        :nMembershipParticipants(c.nMembershipParticipants),
        mapSuperLeafAddr(c.mapSuperLeafAddr),
        BGid(c.BGid),
        SLid(c.SLid)
    {}

    Config& operator = (const Config &c) = default;
};

struct Config parseFromFile(FILE *f);

#endif