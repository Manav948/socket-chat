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
#define backlog 10
#define buffer_size 1024
#define max_client 30
#define room_len 32
#define username_len 32
#define DEFAULT_ROOM "general"

// Define structure to hold client information
typedef struct
{
    SOCKET socket_fd;
    char username[username_len];
    char room[room_len];
    int is_registered;
} Client;

Client clients[max_client];

// Trim newline characters from string
void trim_newline(char *str)
{
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r'))
    {
        str[len - 1] = '\0';
        len--;
    }
}

// Check if username is already taken
int userNameTaken(const char *username)
{
    for (int i = 0; i < max_client; i++)
    {
        if (clients[i].is_registered && strcmp(clients[i].username, username) == 0)
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

// Broadcast message ONLY to clients in the same room
void broadcastToRoom(const char *msg, const char *roomName, SOCKET sender_fd)
{
    for (int i = 0; i < max_client; i++)
    {
        if (clients[i].is_registered &&
            clients[i].socket_fd > 0 &&
            clients[i].socket_fd != sender_fd &&
            stricmp(clients[i].room, roomName) == 0)
        {
            send(clients[i].socket_fd, msg, (int)strlen(msg), 0);
        }
    }
}

// List active rooms
void listRoom(SOCKET sd)
{
    char response[buffer_size];
    snprintf(response, sizeof(response), "\n ACTIVE CHAT ROOMS \n");
    send(sd, response, (int)strlen(response), 0);

    for (int i = 0; i < max_client; i++)
    {
        if (clients[i].is_registered)
        {
            int printed = 0;
            for (int j = 0; j < i; j++)
            {
                if (clients[j].is_registered && stricmp(clients[j].room, clients[i].room) == 0)
                {
                    printed = 1;
                    break;
                }
            }
            if (!printed)
            {
                int count = 0;
                for (int k = 0; k < max_client; k++)
                {
                    if (clients[k].is_registered && stricmp(clients[k].room, clients[i].room) == 0)
                    {
                        count++;
                    }
                }
                snprintf(response, sizeof(response), " - #%s (%d users online)\n", clients[i].room, count);
                send(sd, response, (int)strlen(response), 0);
            }
        }
    }
    const char *footer = "========================================\n\n";
    send(sd, footer, (int)strlen(footer), 0);
}

// Broadcast message to all registered clients except sender
void broadcast_message(const char *msg, SOCKET sender_fd)
{
    for (int i = 0; i < max_client; i++)
    {
        if (clients[i].is_registered && clients[i].socket_fd > 0 && clients[i].socket_fd != sender_fd)
        {
            send(clients[i].socket_fd, msg, (int)strlen(msg), 0);
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

    // Initialize client tracker
    for (int i = 0; i < max_client; i++)
    {
        clients[i].socket_fd = 0;
        clients[i].username[0] = '\0';
        strcpy(clients[i].room, DEFAULT_ROOM);
        clients[i].is_registered = 0;
    }

    // 1. Create Listening Socket
    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET)
    {
        perror("[ERROR] Socket creation failed");
        return 1;
    }

    int op = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&op, sizeof(op));
    printf("[INIT] Listening socket created successfully (FD: %d)\n", (int)server_fd);

    // 2. Bind Socket
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
    printf("[INIT] Socket bound to 0.0.0.0:%d\n", port);

    // 3. Listen
    if (listen(server_fd, backlog) == SOCKET_ERROR)
    {
        perror("[ERROR] Listen failed");
        close_socket(server_fd);
        return 1;
    }

    printf("   REAL-TIME TCP CHAT SERVER RUNNING (PORT: %d)\n", port);
    printf("   Max Capacity: %d clients | Backlog: %d\n", max_client, backlog);

    fd_set readfds;
    char buffer[buffer_size];

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

        // Incoming New Connection
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
                        printf("New connection from %s:%d (Socket FD: %d)\n",
                               client_ip, ntohs(client_addr.sin_port), (int)new_socket);

                        const char *welcomeMessage = "Welcome! Please enter your username: ";
                        send(new_socket, welcomeMessage, (int)strlen(welcomeMessage), 0);
                        break;
                    }
                }
                if (!added)
                {
                    printf("[WARN] Server full! Connection rejected (FD: %d)\n", (int)new_socket);
                    const char *full_msg = "[SERVER] Server full! Disconnecting.\n";
                    send(new_socket, full_msg, (int)strlen(full_msg), 0);
                    close_socket(new_socket);
                }
            }
        }

        // I/O Activity on Client Sockets
        for (int i = 0; i < max_client; i++)
        {
            SOCKET sd = clients[i].socket_fd;
            if (sd > 0 && FD_ISSET(sd, &readfds))
            {
                int bytes_received = recv(sd, buffer, sizeof(buffer) - 1, 0);

                if (bytes_received > 0)
                {
                    buffer[bytes_received] = '\0';
                    trim_newline(buffer);

                    // Client Registration Phase
                    if (!clients[i].is_registered)
                    {
                        if (strlen(buffer) == 0)
                        {
                            const char *err = "[SERVER] Username cannot be empty. Enter username: ";
                            send(sd, err, (int)strlen(err), 0);
                        }
                        else if (userNameTaken(buffer))
                        {
                            const char *err = "[SERVER] Username already taken! Choose another: ";
                            send(sd, err, (int)strlen(err), 0);
                        }
                        else
                        {
                            strncpy(clients[i].username, buffer, username_len - 1);
                            clients[i].username[username_len - 1] = '\0';
                            clients[i].is_registered = 1;

                            printf("[REGISTER] FD %d registered as '%s' (Room: #%s)\n",
                                   (int)sd, clients[i].username, clients[i].room);

                            char welcome_msg[buffer_size];
                            snprintf(welcome_msg, sizeof(welcome_msg),
                                     "Welcome %s! You are in room '#%s'. Commands: /join <room>, /rooms, /msg <user> <msg>\n",
                                     clients[i].username, clients[i].room);
                            send(sd, welcome_msg, (int)strlen(welcome_msg), 0);

                            char join_notification[buffer_size];
                            snprintf(join_notification, sizeof(join_notification),
                                     "[SERVER] *** %s joined room '#%s'! ***\n", clients[i].username, clients[i].room);
                            broadcastToRoom(join_notification, clients[i].room, sd);
                        }
                    }
                    // Chat & Commands Phase
                    else
                    {
                        // COMMAND 1: Join Room (/join <room>)
                        if (strncmp(buffer, "/join ", 6) == 0)
                        {
                            char newRoom[room_len];
                            if (sscanf(buffer, "/join %31s", newRoom) == 1)
                            {
                                if (stricmp(clients[i].room, newRoom) == 0)
                                {
                                    char err[buffer_size];
                                    snprintf(err, sizeof(err), "[SERVER] You are already in room '#%s'.\n", newRoom);
                                    send(sd, err, (int)strlen(err), 0);
                                }
                                else
                                {
                                    // Notify old room
                                    char leaveOld[buffer_size];
                                    snprintf(leaveOld, sizeof(leaveOld), "[SERVER] *** %s left room '#%s' ***\n",
                                             clients[i].username, clients[i].room);
                                    broadcastToRoom(leaveOld, clients[i].room, sd);

                                    // Switch room
                                    strncpy(clients[i].room, newRoom, room_len - 1);
                                    clients[i].room[room_len - 1] = '\0';
                                    printf("[ROOM SWITCH] %s moved to room '#%s'\n", clients[i].username, clients[i].room);

                                    // Confirm to user
                                    char confirm[buffer_size];
                                    snprintf(confirm, sizeof(confirm), "[SERVER] Switched to room '#%s'.\n", clients[i].room);
                                    send(sd, confirm, (int)strlen(confirm), 0);

                                    // Notify new room
                                    char joinNew[buffer_size];
                                    snprintf(joinNew, sizeof(joinNew), "[SERVER] *** %s joined room '#%s'! ***\n",
                                             clients[i].username, clients[i].room);
                                    broadcastToRoom(joinNew, clients[i].room, sd);
                                }
                            }
                        }

                        else if (strcmp(buffer, "/leave") == 0)
                        {
                            if (stricmp(clients[i].room, DEFAULT_ROOM) == 0)
                            {
                                const char *msg = "[SERVER] You are already in default room '#general'.\n";
                                send(sd, msg, (int)strlen(msg), 0);
                            }
                            else
                            {
                                char leaveOld[buffer_size];
                                snprintf(leaveOld, sizeof(leaveOld), "[SERVER] %s left room '#%s'\n",
                                         clients[i].username, clients[i].room);
                                broadcastToRoom(leaveOld, clients[i].room, sd);

                                strcpy(clients[i].room, DEFAULT_ROOM);

                                const char *confirm = "[SERVER] Returned to default room '#general'.\n";
                                send(sd, confirm, (int)strlen(confirm), 0);

                                char joinNew[buffer_size];
                                snprintf(joinNew, sizeof(joinNew), "[SERVER] %s joined room '#general'\n",
                                         clients[i].username);
                                broadcastToRoom(joinNew, DEFAULT_ROOM, sd);
                            }
                        }
                        else if (strcmp(buffer, "/rooms") == 0 || strcmp(buffer, "/room") == 0)
                        {
                            listRoom(sd);
                        }
                        else if (strncmp(buffer, "/msg ", 5) == 0 || strncmp(buffer, "/msg", 4) == 0)
                        {
                            char targetUser[username_len];
                            char privateMsg[buffer_size];
                            if (sscanf(buffer, "/msg %31s %[^\n]", targetUser, privateMsg) == 2)
                            {
                                SOCKET target_fd = findClientByUsername(targetUser);
                                if (target_fd != INVALID_SOCKET)
                                {
                                    printf("[PRIVATE] %s -> %s: %s\n", clients[i].username, targetUser, privateMsg);

                                    char toTarget[buffer_size];
                                    snprintf(toTarget, sizeof(toTarget), "[PRIVATE from %s]: %s\n", clients[i].username, privateMsg);
                                    send(target_fd, toTarget, (int)strlen(toTarget), 0);

                                    char toSender[buffer_size];
                                    snprintf(toSender, sizeof(toSender), "[PRIVATE to %s]: %s\n", targetUser, privateMsg);
                                    send(sd, toSender, (int)strlen(toSender), 0);
                                }
                                else
                                {
                                    char errMsg[buffer_size];
                                    snprintf(errMsg, sizeof(errMsg), "[SERVER] User '%s' not found or not online.\n", targetUser);
                                    send(sd, errMsg, (int)strlen(errMsg), 0);
                                }
                            }
                            else
                            {
                                const char *err = "[SERVER] Invalid private message format. Use: /msg <username> <message>\n";
                                send(sd, err, (int)strlen(err), 0);
                            }
                        }
                        
                        else
                        {
                            printf("[ROOM #%s] %s (FD %d): %s\n", clients[i].room, clients[i].username, (int)sd, buffer);

                            char rmsg[buffer_size];
                            snprintf(rmsg, sizeof(rmsg), "[#%s | %s]: %s\n", clients[i].room, clients[i].username, buffer);
                            broadcastToRoom(rmsg, clients[i].room, sd);
                        }
                    }
                }
                else
                {
                    // Disconnect
                    if (clients[i].is_registered)
                    {
                        printf("[DISCONNECT] User '%s' (FD %d) disconnected from room '#%s'.\n",
                               clients[i].username, (int)sd, clients[i].room);

                        char leave_notification[buffer_size];
                        snprintf(leave_notification, sizeof(leave_notification),
                                 "[SERVER] *** %s left the chat. ***\n", clients[i].username);
                        broadcastToRoom(leave_notification, clients[i].room, sd);
                    }
                    else
                    {
                        printf("[DISCONNECT] Unregistered client (FD %d) disconnected.\n", (int)sd);
                    }

                    close_socket(sd);
                    clients[i].socket_fd = 0;
                    clients[i].is_registered = 0;
                    clients[i].username[0] = '\0';
                    strcpy(clients[i].room, DEFAULT_ROOM);
                }
            }
        }
    }

    printf("Server closing...\n");
    close_socket(server_fd);

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}
