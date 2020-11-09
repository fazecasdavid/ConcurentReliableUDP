#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>

#define MAXPKTSIZE 1024
#define MAXRETRY 5
#define TIMEOUT 5000


// Using this state the application has to handle packet loss and duplication 
typedef struct {
    uint32_t sequenceNo;
    uint32_t totalRetries;
    u_int8_t receiveQueue[MAXPKTSIZE * 5];
    uint32_t receiveQueuelen;

} state_rudp_t;

typedef struct {
    // HEADER
    uint32_t sequenceNo;    // the packet sequence index 
    uint16_t ackFlag;   // indicates packet contains an ACK confirming a previous data packet
    uint32_t ack;   // index of the next expected packet(SequenceNo)
    u_int16_t hasNext;
    // HEADER
    
    uint16_t payloadLen;
    uint8_t payloadData[MAXPKTSIZE];   // the actual data

} mydatagram_packet_t;

state_rudp_t recvState;
state_rudp_t sendState;

int generateError(int err_ratio, int israndom);
int packetSend(int sockfd, mydatagram_packet_t* packetToSend, const struct sockaddr* dest_addr,  size_t destlen);
int packetRecv(int sockfd, mydatagram_packet_t* recvPacket, size_t bufflen, struct sockaddr* src_addr, size_t* srclen, int expectedS);   

int reliableSend(int sockfd, void* buffer, size_t bufflen, const struct sockaddr* dest_addr, size_t destlen) {
    sendState.totalRetries = 0;

    mydatagram_packet_t packetToSend;
    packetToSend.sequenceNo = 0;
    packetToSend.ackFlag = 0;
    packetToSend.ack = 0;

    int bytesSend = 0;
    while(bufflen > 0) {
        if(bufflen >= MAXPKTSIZE){
            memcpy(packetToSend.payloadData, buffer, MAXPKTSIZE);
            packetToSend.payloadLen = MAXPKTSIZE;
            bufflen -= MAXPKTSIZE;
            packetToSend.hasNext = 1;
            bytesSend += MAXPKTSIZE;
        }
        else {
            memcpy(packetToSend.payloadData, buffer, bufflen);
            packetToSend.payloadLen = bufflen;
            bytesSend += bufflen;
            bufflen = 0;
            packetToSend.hasNext = 0;
        }
        int out = packetSend(sockfd, &packetToSend, dest_addr, destlen);
        if(out < 0) {
            return -1;
        }
        
        packetToSend.sequenceNo++;
    }    
    return bytesSend;
}


int reliableRecv(int sockfd, void* buffer, size_t bufflen, struct sockaddr* src_addr, size_t* srclen) {
    state_rudp_t recvState;
    recvState.totalRetries = 0;
    recvState.sequenceNo = 0;

    int out;

    int bytesRead = 0;
    while(bytesRead != bufflen) {
        mydatagram_packet_t recvPacket;
        out = packetRecv(sockfd, &recvPacket, sizeof(recvPacket), src_addr, srclen, recvState.sequenceNo);
        if(out < 0){
            //  printf("Did not receive package!\n");
            return -1;
        }
        recvState.sequenceNo += 1;
        memcpy(buffer + bytesRead, recvPacket.payloadData, recvPacket.payloadLen);
        bytesRead += recvPacket.payloadLen;

        if(recvPacket.hasNext == 0)
            break;
    }
    return bytesRead;

}


int packetSend(int sockfd, mydatagram_packet_t* packetToSend, const struct sockaddr* dest_addr,  size_t destlen) {
    // we send the package and we wait for an ack confiramtion. If we do not recieve an confirmation in
    // TIMEOUT microseconds, we resend the package, all this MAXRETRY times
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = TIMEOUT;

    int packetSize = sizeof(*packetToSend);

    struct sockaddr src_addr;
    memcpy(&src_addr, dest_addr, sizeof(src_addr));
    int packetS = packetToSend->sequenceNo;

    int out;
    for(int i = 0; i < MAXRETRY; i++) {
        out = sendto(sockfd, packetToSend, packetSize, 0, dest_addr, destlen);
        mydatagram_packet_t confirmation;
        int destlencpy = destlen;
        out = recvfrom(sockfd, &confirmation, packetSize, 0, &src_addr, (socklen_t*)&destlencpy);
        if(confirmation.ackFlag == 1)
            if(confirmation.ack == packetS){
                // the package was sent successfully
                return 1;   //return success
            } 

        select(0, NULL, NULL, NULL, &tv); 
        sendState.totalRetries += 1;
        printf("retrimit\n");
    }
    // if we get here, then something went wrong and we didn't get an confirmation
    return -1;
}

int packetRecv(int sockfd, mydatagram_packet_t* recvPacket, size_t bufflen, struct sockaddr* src_addr, size_t* srclen, int expectedS) {
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = TIMEOUT;

    int packetSize = sizeof(*recvPacket);
    int out;
    for(int i = 0; i < MAXRETRY; i++) {
        socklen_t srclencpy = *srclen;
        out = recvfrom(sockfd, recvPacket, packetSize, 0, src_addr, &srclencpy);
        if(out < 0)
            continue;
        
        mydatagram_packet_t confirmation;
        confirmation.ackFlag = 1;
        confirmation.ack = recvPacket->sequenceNo;
        // to emphasize reliability we will generate handmade errors (generateError function)
        if (generateError(3, 0)) {
            recvPacket->sequenceNo = -1;    // pretend we didn t receive the wanted package
            confirmation.ack = recvPacket->sequenceNo;
            printf("I generated an error!\n");
        }


        int confirmationSize = sizeof(confirmation);
        out = sendto(sockfd, &confirmation, confirmationSize, 0, src_addr, *srclen);

        if(recvPacket->sequenceNo == expectedS){
            //printf("received wanted package\n");
            return 1;
        }
        select(0, NULL, NULL, NULL, &tv); 
        sendState.totalRetries += 1;
        printf("reprimesc\n");
    }
    // if we get here, then something went wrong and we didn't get the wanted packet
    return -1;
}



int generateError(int err_ratio, int israndom) {
    static int count = 0;
    //using pure random 0 generation here. err_ratio doesn't count
    if (israndom) {
        if (count == 0) 
            srandom(time(NULL));
        count++;
        return random() % 2;
    }

    int result;
    // constant 1 only if err_ratio=0 and israndom=false
    if (err_ratio == 0)
        result= 1;
    else
    // generate 0 with an err_ratio percentage
        result = count % err_ratio? 0 : 1;
    count++;
    return result;
} 







