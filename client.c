#include "rudp.c"

#define SIZE 1024


int PORT = 7777;
// char* HOST = "192.168.1.203";
char* HOST = "127.0.0.1";

void getfileFromServer(int sockfd, struct sockaddr_in* server, int lengthServer) {

    char filename[55];
    printf("Enter a filename to copy form Server:\n");
    scanf("%s", filename);

    int out = reliableSend(sockfd, filename, 50 * sizeof(char), (struct sockaddr*)server, lengthServer);
    if(out < 0) {
        perror("sendto");
        exit(1);
    }

    int nrBytes;
    // get the size of the file in bytes
    out = reliableRecv(sockfd, &nrBytes, 4, (struct sockaddr*)server, (size_t*)&lengthServer);

    FILE* f = fopen("recived.txt", "wb");
    while(nrBytes > 0) {
        uint8_t buffer[SIZE];
        int bytesRead = reliableRecv(sockfd, buffer, SIZE, (struct sockaddr*)server, (size_t*)&lengthServer);
        if(bytesRead < 0)
            break;
        fwrite(buffer, 1, bytesRead, f);
        nrBytes -= bytesRead;
    }
    fclose(f);
}

int main(int argc, char** argv) {

    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));     // clean before use
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    server.sin_addr.s_addr = inet_addr(HOST);
    int lengthServer = sizeof(server);

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    if(sockfd == -1) {
        perror("socket creation");
        exit(1);
    }
    // if we want to make the server concuret, we need to imitate the TCP connection
    // that is we need another port to communicate with the server.
    // we will let the server choose a port and send it to the client
    // but first, we need to say hello to the server so it will know to handle us
    char hello[] = "Hello\n";
    reliableSend(sockfd, hello, sizeof(hello), (struct sockaddr*)&server, lengthServer);
    // now the server will know that it has to send us a port

    int handler_port;
    int out = reliableRecv(sockfd, &handler_port, 4, (struct sockaddr*)&server, (size_t*)&lengthServer);
    server.sin_port = htons(handler_port);

    printf("I got the port%d\n", handler_port);

    getfileFromServer(sockfd, &server, lengthServer);
    
    return 0;
}

