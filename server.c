#include "rudp.c"
#include <pthread.h>

#define SIZE 1024
#define MAXTHR 20


int PORT = 7777;
char* HOST = "0.0.0.0";

// struct in order to pass data to the pthreadhandler
typedef struct {
    int sockfd;
    struct sockaddr_in client;
    int lengthClient;
} myData;


void* sendFileToClient(void* data) {

    int sockfd = ((myData*)data)->sockfd;
    struct sockaddr_in client = ((myData*)data)->client;
    int lengthClient = ((myData*)data)->lengthClient;

    char filename[55];

    int out = reliableRecv(sockfd, filename, sizeof(char)*50, (struct sockaddr*)&client, (size_t*)&lengthClient);
    if(out < 0) {
        perror("recv filename");
        pthread_exit(NULL);
    }

    FILE* f = fopen(filename, "rb");
    if(f == NULL) {
        perror("Error opening file\n");
        pthread_exit(NULL);
    }

    // get number of bytes in file
    fseek(f, 0L, SEEK_END);
    u_int32_t nrBytes = ftell(f);

    out = reliableSend(sockfd, &nrBytes, 4, (struct sockaddr*)&client, lengthClient);
    if(out < 0) {
        perror("Error seding nrbytes\n");
        pthread_exit(NULL);
    }
    
    // reset the file position indicator to 
    // the beginning of the file
    fseek(f, 0L, SEEK_SET);	

    while(nrBytes > 0) {
        uint8_t buffer[SIZE];
        int bytesRead = fread(buffer, 1, SIZE, f);
        nrBytes -= bytesRead;

        reliableSend(sockfd, buffer, bytesRead, (struct sockaddr*)&client, lengthClient);
    }
    pthread_exit(NULL);
    return NULL;
}

int main(int argc, char** argv) {

    pthread_t threads[MAXTHR];
    myData thread_data[MAXTHR];

    struct sockaddr_in client;
    memset(&client, 0, sizeof(client));     // clean before use
    client.sin_family = AF_INET;
    client.sin_port = htons(PORT);
    client.sin_addr.s_addr = inet_addr(HOST);
    int lengthClient = sizeof(client);
    
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    if(sockfd == -1) {
        perror("socket creation");
        exit(1);
    }
    if(bind(sockfd, (struct sockaddr*)&client, lengthClient) < 0){
        perror("bind");
        exit(1);
    }
    // if we want to make the server concuret, we need to imitate the TCP connection
    // that is we need another port to communicate with the server.
    int thr = 0;
    while(1) {
        char hello[7];
        int out = reliableRecv(sockfd, hello, sizeof(hello), (struct sockaddr*)&client, (size_t*)&lengthClient);
        if(out < 0) {
            continue;
        }
        int handler_port = ++PORT;
        reliableSend(sockfd, &handler_port, 4, (struct sockaddr*)&client, lengthClient);

        memcpy(&thread_data[thr].client, &client, sizeof(client));

        thread_data[thr].client.sin_port = htons(handler_port);
        thread_data[thr].sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        
        if(bind(thread_data[thr].sockfd, (struct sockaddr*)&thread_data[thr].client, lengthClient) < 0){
            perror("bind");
            continue;
        }
        thread_data[thr].lengthClient = lengthClient;
        pthread_create(&threads[thr], NULL, sendFileToClient, &thread_data[thr]);

        thr++;
        if(thr == MAXTHR){
            printf("You have to start the server again!\n");
            break;
        }

    }
    pthread_exit(NULL); // let the threads finish

    return 0;
}

