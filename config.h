#ifndef CONFIG_H_
#define CONFIG_H_

#include <cstdio>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <vector>
struct Config{
    std::vector<std::vector<struct sockaddr_in> > rgrgPeerAddr;
    int BGid;
    int SLid;

    Config(): rgrgPeerAddr(){
        BGid = SLid = 0;
    }

    Config(Config&& c):
        rgrgPeerAddr(std::move(c.rgrgPeerAddr)),
        BGid(c.BGid),
        SLid(c.SLid)
    {}

    Config(const Config& c):
        rgrgPeerAddr(c.rgrgPeerAddr),
        BGid(c.BGid),
        SLid(c.SLid)
    {}

    Config& operator = (const Config &c) = default;
};

struct Config parseFromFile(FILE *f);

#endif