#define _WIN32_WINNT 0x0600

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <stdarg.h>
#include "../include/protocol.h"

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
#define LOG_FILE "server.log"

// PHASE 12: Rate Limiting Parameters (Max 5 messages per 2 seconds)
#define RATE_LIMIT_WINDOW 2 // Seconds
#define RATE_LIMIT_MAX_MSG 5

typedef struct {
    SOCKET socket_fd;
    char username[maxNameLen];
    char room[maxNameLen];
    int is_registered;
    time_t last_msg_time; // Rate limiting: timestamp of last message window
    int msg_count;        // Rate limiting: count of messages in current window
} Client;

Client clients[max_client];
SOCKET server_fd = INVALID_SOCKET;
FILE* log_file_ptr = NULL;

// PHASE 10: Server Logging Function with Timestamps
void log_event(const char* level, const char* format, ...) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

    char message_buf[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(message_buf, sizeof(message_buf), format, args);
    va_end(args);

    // Print to stdout
    printf("[%s] [%s] %s\n", timestamp, level, message_buf);
    fflush(stdout);

    // Log to server.log file
    if (log_file_ptr) {
        fprintf(log_file_ptr, "[%s] [%s] %s\n", timestamp, level, message_buf);
        fflush(log_file_ptr);
    }
}

// Trim newline characters
void trim_newline(char *str) {
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r')) {
        str[len - 1] = '\0';
        len--;
    }
}

// PHASE 12: Input Sanitization (Disallow '|' in usernames to preserve protocol framing)
int is_valid_username(const char *username) {
    if (strlen(username) == 0 || strlen(username) >= maxNameLen) return 0;
    if (strchr(username, '|')) return 0; // Prevent delimiter injection
    if (stricmp(username, "SERVER") == 0) return 0; // Reserved
    return 1;
}

// Check if username is taken
int userNameTaken(const char *username) {
    for (int i = 0; i < max_client; i++) {
        if (clients[i].is_registered && stricmp(clients[i].username, username) == 0) {
            return 1;
        }
    }
    return 0;
}

// Find socket descriptor by username
SOCKET findClientByUsername(const char *username) {
    for (int i = 0; i < max_client; i++) {
        if (clients[i].is_registered && stricmp(clients[i].username, username) == 0) {
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
void broadcastToRoomFrame(const char* type, const char* sender, const char* roomName, const char* payload, SOCKET sender_fd) {
    MessageFrame frame;
    strncpy(frame.type, type, maxTypeLen - 1);
    strncpy(frame.sender, sender, maxNameLen - 1);
    strncpy(frame.target, roomName, maxNameLen - 1);
    strncpy(frame.payload, payload, maxPayloadLen - 1);

    char wire_buf[maxFrameLen];
    serializeFrame(&frame, wire_buf, sizeof(wire_buf));

    for (int i = 0; i < max_client; i++) {
        if (clients[i].is_registered &&
            clients[i].socket_fd > 0 &&
            clients[i].socket_fd != sender_fd &&
            stricmp(clients[i].room, roomName) == 0) {
            send_framed(clients[i].socket_fd, wire_buf, (int)strlen(wire_buf));
        }
    }
}

// PHASE 12: Rate Limiting Check
int is_rate_limited(Client* client) {
    time_t now = time(NULL);
    if (now - client->last_msg_time >= RATE_LIMIT_WINDOW) {
        client->last_msg_time = now;
        client->msg_count = 1;
        return 0; // Allowed
    }

    client->msg_count++;
    if (client->msg_count > RATE_LIMIT_MAX_MSG) {
        return 1; // Rate Limited!
    }
    return 0; // Allowed
}

// PHASE 11: Graceful Server Shutdown Signal Handler (SIGINT / Ctrl+C)
void handle_shutdown(int sig) {
    (void)sig;
    log_event("SHUTDOWN", "Signal received. Initiating graceful server shutdown...");

    // Notify connected clients
    for (int i = 0; i < max_client; i++) {
        if (clients[i].socket_fd > 0) {
            send_frame(clients[i].socket_fd, "SYSTEM", "SERVER", "", "[SERVER] Server is shutting down. Goodbye!");
            close_socket(clients[i].socket_fd);
            clients[i].socket_fd = 0;
        }
    }

    if (server_fd != INVALID_SOCKET) {
        close_socket(server_fd);
        server_fd = INVALID_SOCKET;
    }

    if (log_file_ptr) {
        log_event("SHUTDOWN", "Server shut down gracefully. Closing log file.");
        fclose(log_file_ptr);
        log_file_ptr = NULL;
    }

#ifdef _WIN32
    WSACleanup();
#endif

    exit(0);
}

int main(void) {
    // Register signal handlers for graceful shutdown (Phase 11)
    signal(SIGINT, handle_shutdown);
    signal(SIGTERM, handle_shutdown);

    // Open log file (Phase 10)
    log_file_ptr = fopen(LOG_FILE, "a");
    if (!log_file_ptr) {
        printf("[WARN] Could not open %s for logging.\n", LOG_FILE);
    }

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        log_event("ERROR", "Failed to initialize Winsock.");
        return 1;
    }
#endif

    for (int i = 0; i < max_client; i++) {
        clients[i].socket_fd = 0;
        clients[i].username[0] = '\0';
        strcpy(clients[i].room, DEFAULT_ROOM);
        clients[i].is_registered = 0;
        clients[i].last_msg_time = 0;
        clients[i].msg_count = 0;
    }

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET) {
        log_event("ERROR", "Socket creation failed.");
        return 1;
    }

    int op = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&op, sizeof(op));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        log_event("ERROR", "Bind failed on port %d.", port);
        close_socket(server_fd);
        return 1;
    }

    if (listen(server_fd, backlog) == SOCKET_ERROR) {
        log_event("ERROR", "Listen failed.");
        close_socket(server_fd);
        return 1;
    }

    log_event("INFO", "========================================================");
    log_event("INFO", " REAL-TIME TCP CHAT SERVER STARTED (PORT: %d)", port);
    log_event("INFO", " Security: Rate Limiting & Input Validation Active");
    log_event("INFO", " Observability: Logging events to %s", LOG_FILE);
    log_event("INFO", "========================================================");

    fd_set readfds;
    char buffer[maxFrameLen];

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        SOCKET max_fd = server_fd;

        for (int i = 0; i < max_client; i++) {
            SOCKET sd = clients[i].socket_fd;
            if (sd > 0) {
                FD_SET(sd, &readfds);
            }
            if (sd > max_fd) {
                max_fd = sd;
            }
        }

        int activity = select((int)(max_fd + 1), &readfds, NULL, NULL, NULL);
        if (activity < 0) {
            log_event("ERROR", "Select error encountered.");
            break;
        }

        // EVENT A: Incoming Connection
        if (FD_ISSET(server_fd, &readfds)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            SOCKET new_socket = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
            
            if (new_socket != INVALID_SOCKET) {
                int added = 0;
                for (int i = 0; i < max_client; i++) {
                    if (clients[i].socket_fd == 0) {
                        clients[i].socket_fd = new_socket;
                        clients[i].is_registered = 0;
                        clients[i].username[0] = '\0';
                        strcpy(clients[i].room, DEFAULT_ROOM);
                        clients[i].last_msg_time = time(NULL);
                        clients[i].msg_count = 0;
                        added = 1;

                        char *client_ip = inet_ntoa(client_addr.sin_addr);
                        log_event("CONNECT", "New connection from %s:%d (Socket FD: %d)",
                                  client_ip, ntohs(client_addr.sin_port), (int)new_socket);

                        send_frame(new_socket, "SYSTEM", "SERVER", "", "[SERVER] Welcome! Enter your username:");
                        break;
                    }
                }
                if (!added) {
                    log_event("WARN", "Server full! Connection rejected (FD: %d)", (int)new_socket);
                    send_frame(new_socket, "ERROR", "SERVER", "", "[SERVER] Server full! Disconnecting.");
                    close_socket(new_socket);
                }
            }
        }

        // EVENT B: I/O Activity on Connected Clients
        for (int i = 0; i < max_client; i++) {
            SOCKET sd = clients[i].socket_fd;
            if (sd > 0 && FD_ISSET(sd, &readfds)) {
                int bytes_received = recv_framed(sd, buffer, sizeof(buffer) - 1);

                if (bytes_received > 0) {
                    buffer[bytes_received] = '\0';
                    trim_newline(buffer);

                    // PHASE 12: Rate Limiting Guard Check
                    if (clients[i].is_registered && is_rate_limited(&clients[i])) {
                        log_event("SECURITY", "Rate limit exceeded for user '%s' (FD %d)", clients[i].username, (int)sd);
                        send_frame(sd, "ERROR", "SERVER", "", "[SECURITY] Rate limit exceeded! Please slow down.");
                        continue;
                    }

                    MessageFrame frame;
                    if (!deserializeFrame(buffer, &frame)) {
                        strncpy(frame.type, clients[i].is_registered ? "CHAT" : "REGISTER", maxTypeLen - 1);
                        strncpy(frame.sender, clients[i].username, maxNameLen - 1);
                        strncpy(frame.target, clients[i].room, maxNameLen - 1);
                        strncpy(frame.payload, buffer, maxPayloadLen - 1);
                    }

                    // 1. REGISTRATION PHASE
                    if (!clients[i].is_registered || strcmp(frame.type, "REGISTER") == 0) {
                        const char* proposed_name = (strlen(frame.payload) > 0) ? frame.payload : frame.sender;
                        
                        if (!is_valid_username(proposed_name)) {
                            send_frame(sd, "ERROR", "SERVER", "", "[SERVER] Invalid username! Cannot contain '|' or be reserved.");
                        }
                        else if (userNameTaken(proposed_name)) {
                            send_frame(sd, "ERROR", "SERVER", "", "[SERVER] Username already taken! Choose another:");
                        }
                        else {
                            strncpy(clients[i].username, proposed_name, maxNameLen - 1);
                            clients[i].username[maxNameLen - 1] = '\0';
                            clients[i].is_registered = 1;

                            log_event("REGISTER", "Socket FD %d registered as '%s' (Room: #%s)", 
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
                    else {
                        if (strcmp(frame.type, "PRIVMSG") == 0) {
                            SOCKET target_fd = findClientByUsername(frame.target);
                            if (target_fd != INVALID_SOCKET) {
                                log_event("PRIVMSG", "%s -> %s: %s", clients[i].username, frame.target, frame.payload);
                                send_frame(target_fd, "PRIVMSG", clients[i].username, frame.target, frame.payload);
                                send_frame(sd, "PRIVMSG_ACK", clients[i].username, frame.target, frame.payload);
                            } else {
                                send_frame(sd, "ERROR", "SERVER", frame.target, "[SERVER] User not found or offline.");
                            }
                        }
                        else if (strcmp(frame.type, "JOIN_ROOM") == 0) {
                            if (stricmp(clients[i].room, frame.target) != 0) {
                                char leave_old[maxPayloadLen];
                                snprintf(leave_old, sizeof(leave_old), "[SERVER] %s left room '#%s'", clients[i].username, clients[i].room);
                                broadcastToRoomFrame("SYSTEM", "SERVER", clients[i].room, leave_old, sd);

                                strncpy(clients[i].room, frame.target, maxNameLen - 1);
                                clients[i].room[maxNameLen - 1] = '\0';

                                log_event("ROOM", "%s moved to room '#%s'", clients[i].username, clients[i].room);

                                char confirm[maxPayloadLen];
                                snprintf(confirm, sizeof(confirm), "[SERVER] Switched to room '#%s'.", clients[i].room);
                                send_frame(sd, "SYSTEM", "SERVER", clients[i].room, confirm);

                                char join_new[maxPayloadLen];
                                snprintf(join_new, sizeof(join_new), "[SERVER] %s joined room '#%s'", clients[i].username, clients[i].room);
                                broadcastToRoomFrame("SYSTEM", "SERVER", clients[i].room, join_new, sd);
                            }
                        }
                        else {
                            log_event("CHAT", "#%s | %s: %s", clients[i].room, clients[i].username, frame.payload);
                            broadcastToRoomFrame("CHAT", clients[i].username, clients[i].room, frame.payload, sd);
                        }
                    }
                }
                else {
                    // Disconnect Handling
                    if (clients[i].is_registered) {
                        log_event("DISCONNECT", "User '%s' (FD %d) disconnected.", clients[i].username, (int)sd);
                        char leave_msg[maxPayloadLen];
                        snprintf(leave_msg, sizeof(leave_msg), "[SERVER] %s left the chat.", clients[i].username);
                        broadcastToRoomFrame("SYSTEM", "SERVER", clients[i].room, leave_msg, sd);
                    } else {
                        log_event("DISCONNECT", "Unregistered client (FD %d) disconnected.", (int)sd);
                    }

                    close_socket(sd);
                    clients[i].socket_fd = 0;
                    clients[i].is_registered = 0;
                    clients[i].username[0] = '\0';
                }
            }
        }
    }

    handle_shutdown(0);
    return 0;
}
