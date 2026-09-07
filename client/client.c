#define _WIN32_WINNT 0x0600

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../protocol.h"

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
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    #define close_socket close
#endif

#define server_ip "127.0.0.1"
#define port 8080

// Background Receive Thread: Listens for incoming network messages
#ifdef _WIN32
DWORD WINAPI receive_thread(LPVOID arg) {
    SOCKET sock_fd = *(SOCKET*)arg;
    char buffer[maxFrameLen];

    while (1) {
        int bytes_received = recv_framed(sock_fd, buffer, sizeof(buffer) - 1);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            
            MessageFrame frame;
            if (deserializeFrame(buffer, &frame)) {
                if (strcmp(frame.type, "CHAT") == 0) {
                    // Display incoming message from another user
                    printf("[%s]: %s\n", frame.sender, frame.payload);
                } else if (strcmp(frame.type, "PRIVMSG") == 0) {
                    printf("[Private from %s]: %s\n", frame.sender, frame.payload);
                } else if (strcmp(frame.type, "SYSTEM") == 0) {
                    printf("%s\n", frame.payload);
                } else if (strcmp(frame.type, "ERROR") == 0) {
                    printf("[ERROR]: %s\n", frame.payload);
                }
            } else {
                // If simple text string received
                printf("%s\n", buffer);
            }
            fflush(stdout);
        } else if (bytes_received <= 0) {
            printf("\n[CLIENT] Disconnected from server.\n");
            exit(0);
        }
    }
    return 0;
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

    SOCKET sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd == INVALID_SOCKET) {
        perror("Socket creation failed");
        return 1;
    }
    printf("[CLIENT] Socket created successfully\n");

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(server_ip);

    if (connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        perror("Connect failed");
        close_socket(sock_fd);
        return 1;
    }
    printf("[CLIENT] Connected to server!\n");

#ifdef _WIN32
    CreateThread(NULL, 0, receive_thread, &sock_fd, 0, NULL);
#endif

    char buffer[maxPayloadLen];
    while (1) {
        if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
            break;
        }

        size_t len = strlen(buffer);
        while (len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')) {
            buffer[len - 1] = '\0';
            len--;
        }

        if (strlen(buffer) == 0) continue;

        if (strncmp(buffer, "/quit", 5) == 0) {
            break;
        }

        MessageFrame outFrame;
        memset(&outFrame, 0, sizeof(outFrame));

        if (strncmp(buffer, "/msg ", 5) == 0) {
            strcpy(outFrame.type, "PRIVMSG");
            sscanf(buffer, "/msg %31s %[^\n]", outFrame.target, outFrame.payload);
        } else if (strncmp(buffer, "/join ", 6) == 0) {
            strcpy(outFrame.type, "JOIN_ROOM");
            sscanf(buffer, "/join %31s", outFrame.target);
        } else {
            strcpy(outFrame.type, "CHAT");
            strcpy(outFrame.payload, buffer);
        }

        char wire_buf[maxFrameLen];
        serializeFrame(&outFrame, wire_buf, sizeof(wire_buf));
        send_framed(sock_fd, wire_buf, (int)strlen(wire_buf));

        // LOCAL DISPLAY: Show [YOU]: <message> on sender's own screen instantly!
        if (strcmp(outFrame.type, "CHAT") == 0) {
            printf("[YOU]: %s\n", outFrame.payload);
        } else if (strcmp(outFrame.type, "PRIVMSG") == 0) {
            printf("[Private to %s]: %s\n", outFrame.target, outFrame.payload);
        }
    }

    close_socket(sock_fd);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}