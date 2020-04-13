#include "PeerConnection.h"
#include <sys/socket.h>
#include "exception.h"
#include "message.h"

bool PeerConnection::send(char *src, size_t size){
    size_t sizeSent = 0;
    ssize_t ret = 0;
    while(sizeSent < size){
        ret = ::send(fdSocket, src + sizeSent, size - sizeSent, 0);
        if(ret < 0){
            // Check for EAGAIN and EWOULDBLOCK ?
            throw Exception(Exception::EXCEPTION_SEND);
        }

        sizeSent += ret;
    }
}

void PeerConnection::recv(int socketfd, size_t bytes_to_receive, char * dest){
    size_t received = 0;
    ssize_t ret = 0;
    while(received < bytes_to_receive){
        ret = ::recv(socketfd, dest + received, bytes_to_receive - received, 0);
        if(ret < 0){
            // Check for EAGAIN and EWOULDBLOCK ?
            throw Exception(Exception::EXCEPTION_RECV);
        }

        received += ret;
    }
}