#define _WIN32_WINNT 0x0600

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #pragma comment(lib, "ws2_32.lib")
    #define close_socket closesocket
#else
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <pthread.h>
    typedef int SOCKET;
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    #define close_socket close
#endif

#define server_ip "127.0.0.1"
#define port 8080
#define BUFFER_SIZE 1024

// Dedicated Background Thread: Listens continuously for incoming network messages
#ifdef _WIN32
DWORD WINAPI receive_thread(LPVOID arg) {
    SOCKET sock_fd = *(SOCKET*)arg;
    char buffer[BUFFER_SIZE];

    while (1) {
        int bytes_received = recv(sock_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            // Print incoming broadcast message immediately!
            printf("%s", buffer);
            fflush(stdout);
        } else if (bytes_received == 0) {
            printf("\n[CLIENT] Server closed connection.\n");
            exit(0);
        } else {
            perror("\n[CLIENT] Connection error / lost");
            exit(1);
        }
    }
    return 0;
}
#else
void* receive_thread(void* arg) {
    SOCKET sock_fd = *(SOCKET*)arg;
    char buffer[BUFFER_SIZE];

    while (1) {
        int bytes_received = recv(sock_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            printf("%s", buffer);
            fflush(stdout);
        } else if (bytes_received == 0) {
            printf("\n[CLIENT] Server closed connection.\n");
            exit(0);
        } else {
            perror("\n[CLIENT] Connection error / lost");
            exit(1);
        }
    }
    return NULL;
}
#endif

int main(void) {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
        printf("Failed to initialize Winsock.\n");
        return 1;
    }
#endif

    // 1. Create socket
    SOCKET sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd == INVALID_SOCKET) {
        perror("Socket creation failed");
        return 1;
    }
    printf("[CLIENT] Socket created successfully\n");

    // 2. Specify server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(server_ip);

    if (server_addr.sin_addr.s_addr == INADDR_NONE) {
        printf("[CLIENT] Invalid IP address\n");
        close_socket(sock_fd);
        return 1;
    }
    printf("[CLIENT] Connecting to server at %s:%d...\n", server_ip, port);

    // 3. Connect to server
    if (connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        perror("Connect failed");
        close_socket(sock_fd);
        return 1;
    }
    printf("[CLIENT] Successfully connected to server!\n");

    // 4. Start Background Receive Thread so incoming messages display INSTANTLY
#ifdef _WIN32
    HANDLE recv_thread = CreateThread(NULL, 0, receive_thread, &sock_fd, 0, NULL);
    if (recv_thread == NULL) {
        printf("[CLIENT] Failed to create receive thread.\n");
        close_socket(sock_fd);
        return 1;
    }
#else
    pthread_t recv_thread;
    if (pthread_create(&recv_thread, NULL, receive_thread, &sock_fd) != 0) {
        printf("[CLIENT] Failed to create receive thread.\n");
        close_socket(sock_fd);
        return 1;
    }
#endif

    // 5. Main Thread Loop: Handles user keyboard input & sending
    char buffer[BUFFER_SIZE];
    while (1) {
        if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
            break;
        }

        if (strncmp(buffer, "/quit", 5) == 0) {
            printf("[CLIENT] Disconnecting...\n");
            break;
        }

        printf("[YOU]: %s", buffer);

        // Send message to server (Receive Thread will catch any responses/broadcasts asynchronously!)
        int bytes_sent = send(sock_fd, buffer, (int)strlen(buffer), 0);
        if (bytes_sent == SOCKET_ERROR) {
            perror("[ERROR] Send failed");
            break;
        }
    }

    close_socket(sock_fd);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}