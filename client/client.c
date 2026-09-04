#define _WIN32_WINNT 0x0600

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
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

#define server_ip "127.0.0.1"
#define port 8080
#define BUFFER_SIZE 1024

int main(void) {
    #ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0){
        printf("Failed to initialize winsock.\n");
        return 1;
    }
    #endif
    //  create socket
    SOCKET sock_fd = socket(AF_INET, SOCK_STREAM , 0);
    if(sock_fd == INVALID_SOCKET) {
        perror("socket creation failed");
        return 1;
    }
    printf("Socket created successfully \n");

    // specify server address and port

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(server_ip);

    if (server_addr.sin_addr.s_addr == INADDR_NONE) {
        printf("[CLIENT] Invalid IP address / Address not supported\n");
        close_socket(sock_fd);
        return 1;
    }
    printf("[CLIENT] Connecting to server at %s:%d...\n", server_ip, port);

    // connect to server (Triggers TCP 3-Way Handshake: SYN -> SYN-ACK -> ACK)
    if(connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR){
        perror("connect failed");
        close_socket(sock_fd);
        return 1;
    }
    printf("[CLIENT] Successfully connected to server!\n");

    // receive response from server
    char buffer[BUFFER_SIZE];
    int bytes_received = recv(sock_fd, buffer, sizeof(buffer) - 1, 0);

    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        printf("[CLIENT] Received reply from server: \"%s\" (%d bytes)\n", buffer, bytes_received);
    }
    // iterative loop for multiple clints
    while(1) {
        printf("You >");
        if(fgets(buffer, sizeof(buffer), stdin) == NULL) {
            break;
        }
        // check for disconnect command
        if(strncmp(buffer, "/quit", 5) == 0) {
            printf("[CLIENT] DisConnect...\n");
            break;
        }
        int bytes_sent = send(sock_fd, buffer, strlen(buffer), 0);
        if(bytes_sent == SOCKET_ERROR) {
            perror("send failed");
            break;
        }
        bytes_received = recv(sock_fd, buffer, sizeof(buffer) - 1 , 0);

        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            printf("[CLIENT] Received reply from server: \"%s\" (%d bytes)\n", buffer, bytes_received);
        } else if (bytes_received == 0) {
            printf("[CLIENT] Server closed the connection\n");
            break;
        } else {
            perror("recv failed");
            break;
        }
    }
    

    // close socket
    printf("[CLIENT] Closing the socket\n");
    close_socket(sock_fd);

    #ifdef _WIN32
        WSACleanup();
    #endif

    return 0;
}