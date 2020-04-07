#include "config.h"
#include "exception.h"
#include <cstring>

struct Config parseFromFile(FILE *f){
    Config ret;
    int BGnum = 0;
    if(fscanf(f, "%d", &BGnum) != 1){
        throw Exception(Exception::EXCEPTION_INPUT_FORMAT);
    }

    char tmpstr[41];
    struct sockaddr_in peerAddr;
    struct in_addr tmpaddr;
    short tmpport;
    int BGid = 0;
    int SLid = 0;

    for(int i = 0; i < BGnum; ++i){
        ret.rgrgPeerAddr.push_back(std::vector<struct sockaddr_in>());
        int SLnum = 0;
        if(fscanf(f, "%d", &SLnum) != 1){
            throw Exception(Exception::EXCEPTION_INPUT_FORMAT);
        }

        for(int j = 0; j < SLnum; ++j){
            if(fscanf(f, "%40s", tmpstr) != 1){
                throw Exception(Exception::EXCEPTION_INPUT_FORMAT);
            }

            if(inet_aton(tmpstr, &tmpaddr) == 0){
                throw Exception(Exception::EXCEPTION_INPUT_FORMAT);
            }

            memset(&peerAddr, 0, sizeof(peerAddr));
            peerAddr.sin_family = AF_INET;
            peerAddr.sin_addr = tmpaddr;
            if(fscanf(f, "%hd", &tmpport) != 1){
                throw Exception(Exception::EXCEPTION_INPUT_FORMAT);
            }

            peerAddr.sin_port = htons(tmpport);
            ret.rgrgPeerAddr[i].push_back(peerAddr);
        }
    }

    if(fscanf(f, "%d%d", &ret.BGid, &ret.SLid) != 2){
        throw Exception(Exception::EXCEPTION_INPUT_FORMAT);
    }

    if(ret.BGid < 0 || ret.SLid < 0){
        throw Exception(Exception::EXCEPTION_INPUT_FORMAT);
    }

    if(ret.BGid >= ret.rgrgPeerAddr.size()){
        throw Exception(Exception::EXCEPTION_INPUT_FORMAT);
    }
    else if(ret.SLid >= ret.rgrgPeerAddr[ret.BGid].size()){
        throw Exception(Exception::EXCEPTION_INPUT_FORMAT);
    }

    return ret;
}