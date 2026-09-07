#define _WIN32_WINNT 0x0600

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../protocol.h"

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
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define close_socket close
#endif

#define port 8080
#define backlog 10
#define buffer_size 1024
#define max_client 30
#define DEFAULT_ROOM "general"

typedef struct
{
    SOCKET socket_fd;
    char username[maxNameLen];
    char room[maxNameLen];
    int is_registered;
} Client;

Client clients[max_client];

// Trim newline characters
void trim_newline(char *str)
{
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r'))
    {
        str[len - 1] = '\0';
        len--;
    }
}

// Check if username is taken
int userNameTaken(const char *username)
{
    for (int i = 0; i < max_client; i++)
    {
        if (clients[i].is_registered && stricmp(clients[i].username, username) == 0)
        {
            return 1;
        }
    }
    return 0;
}

// Find socket descriptor by username
SOCKET findClientByUsername(const char *username)
{
    for (int i = 0; i < max_client; i++)
    {
        if (clients[i].is_registered && stricmp(clients[i].username, username) == 0)
        {
            return clients[i].socket_fd;
        }
    }
    return INVALID_SOCKET;
}

// Send length-prefixed protocol frame to a single socket
void send_frame(SOCKET sd, const char* type, const char* sender, const char* target, const char* payload) {
    MessageFrame frame;
    strncpy(frame.type, type, maxTypeLen - 1);
    strncpy(frame.sender, sender, maxNameLen - 1);
    strncpy(frame.target, target, maxNameLen - 1);
    strncpy(frame.payload, payload, maxPayloadLen - 1);

    char wire_buf[maxFrameLen];
    serializeFrame(&frame, wire_buf, sizeof(wire_buf));
    send_framed(sd, wire_buf, (int)strlen(wire_buf));
}

// Broadcast length-prefixed protocol frame to all clients in room except sender
void broadcastToRoomFrame(const char* type, const char* sender, const char* roomName, const char* payload, SOCKET sender_fd)
{
    MessageFrame frame;
    strncpy(frame.type, type, maxTypeLen - 1);
    strncpy(frame.sender, sender, maxNameLen - 1);
    strncpy(frame.target, roomName, maxNameLen - 1);
    strncpy(frame.payload, payload, maxPayloadLen - 1);

    char wire_buf[maxFrameLen];
    serializeFrame(&frame, wire_buf, sizeof(wire_buf));

    for (int i = 0; i < max_client; i++)
    {
        if (clients[i].is_registered &&
            clients[i].socket_fd > 0 &&
            clients[i].socket_fd != sender_fd &&
            stricmp(clients[i].room, roomName) == 0)
        {
            send_framed(clients[i].socket_fd, wire_buf, (int)strlen(wire_buf));
        }
    }
}

int main(void)
{
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        printf("[ERROR] Failed to initialize Winsock.\n");
        return 1;
    }
#endif

    for (int i = 0; i < max_client; i++)
    {
        clients[i].socket_fd = 0;
        clients[i].username[0] = '\0';
        strcpy(clients[i].room, DEFAULT_ROOM);
        clients[i].is_registered = 0;
    }

    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET)
    {
        perror("[ERROR] Socket creation failed");
        return 1;
    }

    int op = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&op, sizeof(op));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR)
    {
        perror("[ERROR] Bind failed");
        close_socket(server_fd);
        return 1;
    }

    if (listen(server_fd, backlog) == SOCKET_ERROR)
    {
        perror("[ERROR] Listen failed");
        close_socket(server_fd);
        return 1;
    }

    printf("   REAL-TIME TCP CHAT SERVER RUNNING (PORT: %d)\n", port);


    fd_set readfds;
    char buffer[maxFrameLen];

    while (1)
    {
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        SOCKET max_fd = server_fd;

        for (int i = 0; i < max_client; i++)
        {
            SOCKET sd = clients[i].socket_fd;
            if (sd > 0)
            {
                FD_SET(sd, &readfds);
            }
            if (sd > max_fd)
            {
                max_fd = sd;
            }
        }

        int activity = select((int)(max_fd + 1), &readfds, NULL, NULL, NULL);
        if (activity < 0)
        {
            perror("[ERROR] Select error");
            break;
        }

        // EVENT A: Incoming Connection
        if (FD_ISSET(server_fd, &readfds))
        {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            SOCKET new_socket = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
            if (new_socket != INVALID_SOCKET)
            {
                int added = 0;
                for (int i = 0; i < max_client; i++)
                {
                    if (clients[i].socket_fd == 0)
                    {
                        clients[i].socket_fd = new_socket;
                        clients[i].is_registered = 0;
                        clients[i].username[0] = '\0';
                        strcpy(clients[i].room, DEFAULT_ROOM);
                        added = 1;

                        char *client_ip = inet_ntoa(client_addr.sin_addr);
                        printf("[CONNECT] Connection from %s:%d (FD: %d)\n",
                               client_ip, ntohs(client_addr.sin_port), (int)new_socket);

                        send_frame(new_socket, "SYSTEM", "SERVER", "", "[SERVER] Welcome! Enter your username:");
                        break;
                    }
                }
                if (!added)
                {
                    send_frame(new_socket, "ERROR", "SERVER", "", "[SERVER] Server full! Disconnecting.");
                    close_socket(new_socket);
                }
            }
        }

        // EVENT B: Activity on Connected Clients
        for (int i = 0; i < max_client; i++)
        {
            SOCKET sd = clients[i].socket_fd;
            if (sd > 0 && FD_ISSET(sd, &readfds))
            {
                int bytes_received = recv_framed(sd, buffer, sizeof(buffer) - 1);

                if (bytes_received > 0)
                {
                    buffer[bytes_received] = '\0';
                    trim_newline(buffer);

                    MessageFrame frame;
                    if (!deserializeFrame(buffer, &frame)) {
                        strncpy(frame.type, clients[i].is_registered ? "CHAT" : "REGISTER", maxTypeLen - 1);
                        strncpy(frame.sender, clients[i].username, maxNameLen - 1);
                        strncpy(frame.target, clients[i].room, maxNameLen - 1);
                        strncpy(frame.payload, buffer, maxPayloadLen - 1);
                    }

                    // 1. REGISTRATION PHASE
                    if (!clients[i].is_registered || strcmp(frame.type, "REGISTER") == 0)
                    {
                        const char* proposed_name = (strlen(frame.payload) > 0) ? frame.payload : frame.sender;
                        if (strlen(proposed_name) == 0)
                        {
                            send_frame(sd, "ERROR", "SERVER", "", "[SERVER] Username cannot be empty. Enter username:");
                        }
                        else if (userNameTaken(proposed_name))
                        {
                            send_frame(sd, "ERROR", "SERVER", "", "[SERVER] Username already taken! Choose another:");
                        }
                        else
                        {
                            strncpy(clients[i].username, proposed_name, maxNameLen - 1);
                            clients[i].username[maxNameLen - 1] = '\0';
                            clients[i].is_registered = 1;
                            
                            printf("[REGISTER] FD %d registered as '%s' (Room: #%s)\n", 
                                   (int)sd, clients[i].username, clients[i].room);

                            char welcome_msg[maxPayloadLen];
                            snprintf(welcome_msg, sizeof(welcome_msg),
                                     "[SERVER] Welcome, %s! You are in room '#%s'. Commands: /join <room>, /rooms, /msg <user> <msg>",
                                     clients[i].username, clients[i].room);
                            send_frame(sd, "SYSTEM", "SERVER", clients[i].room, welcome_msg);

                            char join_msg[maxPayloadLen];
                            snprintf(join_msg, sizeof(join_msg), "[SERVER] %s joined room '#%s'", clients[i].username, clients[i].room);
                            broadcastToRoomFrame("SYSTEM", "SERVER", clients[i].room, join_msg, sd);
                        }
                    }
                    // 2. CHAT & COMMAND PROTOCOL PHASE
                    else
                    {
                        if (strcmp(frame.type, "PRIVMSG") == 0)
                        {
                            SOCKET target_fd = findClientByUsername(frame.target);
                            if (target_fd != INVALID_SOCKET)
                            {
                                printf("[PRIVATE] %s -> %s: %s\n", clients[i].username, frame.target, frame.payload);
                                send_frame(target_fd, "PRIVMSG", clients[i].username, frame.target, frame.payload);
                                send_frame(sd, "PRIVMSG_ACK", clients[i].username, frame.target, frame.payload);
                            }
                            else
                            {
                                send_frame(sd, "ERROR", "SERVER", frame.target, "[SERVER] User not found or offline.");
                            }
                        }
                        else if (strcmp(frame.type, "JOIN_ROOM") == 0)
                        {
                            if (stricmp(clients[i].room, frame.target) != 0)
                            {
                                char leave_old[maxPayloadLen];
                                snprintf(leave_old, sizeof(leave_old), "[SERVER] %s left room '#%s'", clients[i].username, clients[i].room);
                                broadcastToRoomFrame("SYSTEM", "SERVER", clients[i].room, leave_old, sd);

                                strncpy(clients[i].room, frame.target, maxNameLen - 1);
                                clients[i].room[maxNameLen - 1] = '\0';

                                printf("[ROOM SWITCH] %s moved to room '#%s'\n", clients[i].username, clients[i].room);

                                char confirm[maxPayloadLen];
                                snprintf(confirm, sizeof(confirm), "[SERVER] Switched to room '#%s'.", clients[i].room);
                                send_frame(sd, "SYSTEM", "SERVER", clients[i].room, confirm);

                                char join_new[maxPayloadLen];
                                snprintf(join_new, sizeof(join_new), "[SERVER] %s joined room '#%s'", clients[i].username, clients[i].room);
                                broadcastToRoomFrame("SYSTEM", "SERVER", clients[i].room, join_new, sd);
                            }
                        }
                        else // Standard CHAT frame
                        {
                            printf("[CHAT #%s] %s: %s\n", clients[i].room, clients[i].username, frame.payload);
                            broadcastToRoomFrame("CHAT", clients[i].username, clients[i].room, frame.payload, sd);
                        }
                    }
                }
                else
                {
                    // Disconnect
                    if (clients[i].is_registered)
                    {
                        printf("[DISCONNECT] User '%s' (FD %d) disconnected.\n", clients[i].username, (int)sd);
                        char leave_msg[maxPayloadLen];
                        snprintf(leave_msg, sizeof(leave_msg), "[SERVER] %s left the chat.", clients[i].username);
                        broadcastToRoomFrame("SYSTEM", "SERVER", clients[i].room, leave_msg, sd);
                    }

                    close_socket(sd);
                    clients[i].socket_fd = 0;
                    clients[i].is_registered = 0;
                    clients[i].username[0] = '\0';
                }
            }
        }
    }

    close_socket(server_fd);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
