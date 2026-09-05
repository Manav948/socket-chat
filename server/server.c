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
#define username_len 32

// Define structure to hold client information
typedef struct
{
    SOCKET socket_fd;
    char username[username_len];
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

// Helper: Broadcast a message to all registered clients except sender (if sender_fd != 0)
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
                        added = 1;

                        char *client_ip = inet_ntoa(client_addr.sin_addr);
                        printf("[CONNECT] New connection from %s:%d (Socket FD: %d)\n",
                               client_ip, ntohs(client_addr.sin_port), (int)new_socket);

                        const char *welcomeMessage = "[SERVER] Welcome! Please enter your username: ";
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

        //  I/O Activity on Client Sockets
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

                            printf("[REGISTER] FD %d registered as '%s'\n", (int)sd, clients[i].username);

                            // Send confirmation to registered user
                            char welcome_msg[buffer_size];
                            snprintf(welcome_msg, sizeof(welcome_msg),
                                     "[SERVER] Welcome, %s! Type your message and hit Enter.\n", clients[i].username);
                            send(sd, welcome_msg, (int)strlen(welcome_msg), 0);

                            // Broadcast join notification to all OTHER connected users!
                            char join_notification[buffer_size];
                            snprintf(join_notification, sizeof(join_notification),
                                     "[SERVER] *** %s joined the chat! ***\n", clients[i].username);
                            broadcast_message(join_notification, sd);
                        }
                    }
                    // Chat Broadcast Phase
                    else
                    {
                        printf("[CHAT] %s (FD %d): %s\n", clients[i].username, (int)sd, buffer);

                        // Broadcast chat message to ALL OTHER clients!
                        char broadcast_msg[buffer_size];
                        snprintf(broadcast_msg, sizeof(broadcast_msg), "[%s]: %s\n", clients[i].username, buffer);
                        broadcast_message(broadcast_msg, sd);
                    }
                }
                else
                {
                    // Client Disconnection Handling
                    if (clients[i].is_registered)
                    {
                        printf("[DISCONNECT] User '%s' (FD %d) disconnected.\n", clients[i].username, (int)sd);

                        // Broadcast leave notification to remaining users
                        char leave_notification[buffer_size];
                        snprintf(leave_notification, sizeof(leave_notification),
                                 "[SERVER] *** %s left the chat. ***\n", clients[i].username);
                        broadcast_message(leave_notification, sd);
                    }
                    else
                    {
                        printf("[DISCONNECT] Unregistered client (FD %d) disconnected.\n", (int)sd);
                    }

                    close_socket(sd);
                    clients[i].socket_fd = 0;
                    clients[i].is_registered = 0;
                    clients[i].username[0] = '\0';
                }
            }
        }
    }

    printf("[SHUTDOWN] Server closing...\n");
    close_socket(server_fd);

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}
