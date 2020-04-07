#include "config.h"
#include "exception.h"
#include <cstring>

struct Config parseFromFile(FILE *f){
    Config ret;
    int num;
    fscanf(f, "%d", &num);
    ret.nMembershipParticipants = num;

    char tmpstr[41];
    struct sockaddr_in peerAddr;
    struct in_addr tmpaddr;
    short tmpport;
    int BGid;
    int SLid;

    for(int i = 0; i < num; ++i){
        if(fscanf(f, "%d%d", &BGid, &SLid) != 2){
            throw Exception(Exception::EXCEPTION_INPUT_FORMAT);
        }
        
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
        std::pair<int, int> pairIDs(BGid, SLid);
        ret.mapSuperLeafAddr.insert(std::make_pair(pairIDs, peerAddr));
    }

    if(fscanf(f, "%d%d", &ret.BGid, &ret.SLid) != 2){
        throw Exception(Exception::EXCEPTION_INPUT_FORMAT);
    }
    
    if(ret.mapSuperLeafAddr.find(std::make_pair(ret.BGid, ret.SLid)) == ret.mapSuperLeafAddr.end()){
        // The ID is not in the given address list
        throw Exception(Exception::EXCEPTION_INPUT_FORMAT);
    }

    return ret;
}