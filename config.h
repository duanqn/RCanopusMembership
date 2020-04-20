#ifndef CONFIG_H_
#define CONFIG_H_

#include <cstdio>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <vector>

struct BGInfo{
    int failure;
    std::vector<struct sockaddr_in> addr;
};

struct Config{
    std::vector<BGInfo> rgBGInfo;
    int BGid;
    int SLid;
    int globalFailures;
    int requestPerSLPerSecond;

    Config(): rgBGInfo(){
        BGid = SLid = 0;
    }

    Config(Config&& c):
        rgBGInfo(std::move(c.rgBGInfo)),
        BGid(c.BGid),
        SLid(c.SLid),
        requestPerSLPerSecond(c.requestPerSLPerSecond)
    {}

    Config(const Config& c):
        rgBGInfo(c.rgBGInfo),
        BGid(c.BGid),
        SLid(c.SLid),
        requestPerSLPerSecond(c.requestPerSLPerSecond)
    {}

    Config& operator = (const Config &c) = default;

    inline int numBG() const {
        return rgBGInfo.size();
    }

    inline int numSL(int BGid) const {
        return rgBGInfo[BGid].addr.size();
    }

    inline int getF(int BGid) const {
        return rgBGInfo[BGid].failure;
    }
};

struct Config parseFromFile(FILE *f);

#endif