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

    for(int i = 0; i < num; ++i){
        fscanf(f, "%40s", tmpstr);
        if(inet_aton(tmpstr, &tmpaddr) == 0){
            throw Exception(Exception::EXCEPTION_INPUT_FORMAT);
        }

        memset(&peerAddr, 0, sizeof(peerAddr));
        peerAddr.sin_family = AF_INET;
        peerAddr.sin_addr = tmpaddr;
        fscanf(f, "%hd", &tmpport);
        peerAddr.sin_port = htons(tmpport);

        ret.rgPeerAddr.push_back(peerAddr);
    }

    return ret;
}