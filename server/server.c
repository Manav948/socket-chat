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
#define max_client 30

int main(void)
{
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        printf("Failed to initialize winsock. \n");
        return 1;
    }
#endif

    // track active client
    SOCKET client_sockets[max_client];
    for (int i = 0; i < max_client; i++)
    {
        client_sockets[i] = 0;
    }

    // create listening socket
    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET)
    {
        perror("Socket create failed");
        return 1;
    }

    // re-ues socket for multiple client connection
    int op = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&op, sizeof(op));
    printf("Socket created successfully (FD: %d)\n", (int)server_fd);

    // bind socket to ip and port
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR)
    {
        perror("Bind failed");
        close_socket(server_fd);
        return 1;
    }
    printf("Socket bound to port %d\n", port);

    // listen for incoming connection
    if (listen(server_fd, backlog) == SOCKET_ERROR)
    {
        perror("Listen failed");
        close_socket(server_fd);
        return 1;
    }
    printf("Server is listening for incoming connections on port %d (backlog: %d)...\n", port, backlog);

    // accept connection
    fd_set readfds;
    char buffer[buffer_size];

    while (1)
    {
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        SOCKET max_fd = server_fd;

        for (int i = 0; i < max_client; i++)
        {
            SOCKET sd = client_sockets[i];
            if (sd > 0)
            {
                FD_SET(sd, &readfds);
            }
            if (sd > max_fd)
            {
                max_fd = sd;
            }
        }
        // Wait for activity on one of the sockets
        int activity = select((int)(max_fd + 1), &readfds, NULL, NULL, NULL);
        if (activity < 0)
        {
            perror("select error");
            break;
        }

        //  Incoming New Connection on Listening Socket

        if (FD_ISSET(server_fd, &readfds))
        {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            SOCKET new_socket = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
            if (new_socket != INVALID_SOCKET)
            {
                char *client_ip = inet_ntoa(client_addr.sin_addr);
                printf("[SERVER] New connection! IP: %s, Port: %d (Socket FD: %d)\n", client_ip,
                       ntohs(client_addr.sin_port), (int)new_socket);

                const char *welcomeMessage = "Welcome to the server!";
                send(new_socket, welcomeMessage, (int)strlen(welcomeMessage), 0);

                // add new socket into array of sockets
                int added = 0;
                for (int i = 0; i < max_client; i++)
                {
                    if (client_sockets[i] == 0)
                    {
                        client_sockets[i] = new_socket;
                        added = 1;
                        printf("[SERVER] Added client to slot %d\n", i);
                        break;
                    }
                }
                if (!added)
                {
                    printf("[SERVER] Server full! Rejecting client.\n");
                    close_socket(new_socket);
                }
            }
        }
        // IO operation on some other socket
        for(int i = 0; i < max_client ; i++) {
            SOCKET sd = client_sockets[i];
            if(sd > 0 && FD_ISSET(sd, &readfds)) {
                int bytes_received = recv(sd, buffer, sizeof(buffer) - 1, 0);
                
                if(bytes_received > 0) {
                    buffer[bytes_received] = '\0';
                     printf("[CLIENT FD %d]: %s", (int)sd, buffer);

                    char replay[buffer_size];
                    snprintf(replay, sizeof(replay), "Server received: %s", buffer);
                    send(sd, replay, (int)strlen(replay), 0);
                }else if(bytes_received == 0) {
                    //  client dis-connected
                    struct sockaddr_in client_addr;
                    socklen_t client_len = sizeof(client_addr);
                    getpeername(sd, (struct sockaddr *)&client_addr, &client_len);
                    printf("[SERVER] Client disconnected! IP: %s, Port: %d (Socket FD: %d)\n", inet_ntoa(client_addr.sin_addr),
                           ntohs(client_addr.sin_port), (int)sd);
                    close_socket(sd);
                    client_sockets[i] = 0;
                }else {
                    perror("recv failed");
                    close_socket(sd);
                    client_sockets[i] = 0;
                }
            }
        }
    }
    // close connection
    printf("Closing the connection\n");
    close_socket(server_fd);
    close_socket(server_fd);

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}
