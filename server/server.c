#define _WIN32_WINNT 0x0600

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
    #define close_socket closesocket
#else 
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    typedef int SOCKET;
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    #define close_socket close
#endif

#define port 8080
#define backlog 5
#define buffer_size 1024

int main (void) {
    #ifdef _WIN32
    WSADATA wsaData;
    if(WSAStartup(MAKEWORD(2,2), &wsaData) != 0){
        printf("Failed to initialize winsock. \n");
        return 1;
    }
    #endif

    // create listening socket
    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_fd == INVALID_SOCKET) {
        perror("Socket create failed");
        return 1;
    }
    printf("Socket created successfully (FD: %d)\n", (int)server_fd);
    
    // bind socket to ip and port
    struct sockaddr_in server_addr;
    memset(&server_addr , 0 ,sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if(bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR){
        perror("Bind failed");
        close_socket(server_fd);
        return 1;
    }
    printf("Socket bound to port %d\n", port);

    // listen for incoming connection
    if(listen(server_fd , backlog) == SOCKET_ERROR){
        perror("Listen failed");
        close_socket(server_fd);
        return 1;
    }
    printf("Server is listening for incoming connections on port %d (backlog: %d)...\n", port, backlog);

    // accept connection
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    SOCKET client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
    if(client_fd == INVALID_SOCKET ){
        perror("Accpect failed");
        close_socket(server_fd);
        return 1;
    }

    char* client_ip = inet_ntoa(client_addr.sin_addr);
    printf("Client connected! IP: %s, Port: %d (Client FD: %d)\n", client_ip, ntohs(client_addr.sin_port), (int)client_fd);

    char buffer[buffer_size];
    int bytes_received = recv(client_fd, buffer , sizeof(buffer) -1 , 0);
    
    // check 
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        printf("[SERVER] Received from client: \"%s\" (%d bytes)\n", buffer, bytes_received);

        const char* server_reply = "Hello client, message received successfully!";
        int bytes_sent = send(client_fd, server_reply, (int)strlen(server_reply), 0);

        if (bytes_sent == SOCKET_ERROR) {
            perror("Send failed");
        } else {
            printf("[SERVER] Sent reply to client (%d bytes)\n", bytes_sent);
        }
    } else if (bytes_received == 0) {
        printf("[SERVER] Client disconnected before sending data.\n");
    } else {
        perror("recv failed");
    }

    // close connection
    printf("Closing the connection\n");
    close_socket(client_fd);
    close_socket(server_fd);

    #ifdef _WIN32
    WSACleanup();
    #endif

    return 0;

 }
       