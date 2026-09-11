#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    typedef int SOCKET;
#endif

#define maxTypeLen 16
#define maxNameLen 32
#define maxPayloadLen 1024
#define maxFrameLen 1120

// Protocol Message Frame Structure
typedef struct {
    char type[maxTypeLen];       // REGISTER, CHAT, PRIVMSG, JOIN_ROOM, SYSTEM, ERROR
    char sender[maxNameLen];     // Username of sender or "SERVER"
    char target[maxNameLen];     // Target room name or target username
    char payload[maxPayloadLen]; // Message payload
} MessageFrame;

// Serialize: Convert MessageFrame struct into wire-format string ("TYPE|FROM|TO|PAYLOAD")
static inline void serializeFrame(const MessageFrame* frame, char* out_buffer, size_t max_len) {
    snprintf(out_buffer, max_len, "%s|%s|%s|%s", frame->type, frame->sender, frame->target, frame->payload);
}

// Deserialize: Parse wire-format string ("TYPE|FROM|TO|PAYLOAD") into MessageFrame struct
static inline int deserializeFrame(const char* inBuffer, MessageFrame* frame) {
    memset(frame, 0, sizeof(MessageFrame));

    const char* p1 = strchr(inBuffer, '|');
    if (!p1) return 0;

    const char* p2 = strchr(p1 + 1, '|');
    if (!p2) return 0;

    const char* p3 = strchr(p2 + 1, '|');
    if (!p3) return 0;

    // 1. Extract type
    size_t type_len = p1 - inBuffer;
    if (type_len >= maxTypeLen) type_len = maxTypeLen - 1;
    strncpy(frame->type, inBuffer, type_len);
    frame->type[type_len] = '\0';

    // 2. Extract sender
    size_t sender_len = p2 - (p1 + 1);
    if (sender_len >= maxNameLen) sender_len = maxNameLen - 1;
    strncpy(frame->sender, p1 + 1, sender_len);
    frame->sender[sender_len] = '\0';

    // 3. Extract target
    size_t target_len = p3 - (p2 + 1);
    if (target_len >= maxNameLen) target_len = maxNameLen - 1;
    strncpy(frame->target, p2 + 1, target_len);
    frame->target[target_len] = '\0';

    // 4. Extract payload
    strncpy(frame->payload, p3 + 1, maxPayloadLen - 1);
    frame->payload[maxPayloadLen - 1] = '\0';

    return 1;
}

// Send framed message over TCP (4-byte big-endian length prefix header + payload)
static inline int send_framed(SOCKET sd, const char* buffer, int len) {
    uint32_t net_len = htonl((uint32_t)len);
    if (send(sd, (const char*)&net_len, sizeof(net_len), 0) <= 0) return -1;
    if (send(sd, buffer, len, 0) <= 0) return -1;
    return 0;
}

// Receive framed message over TCP (reads exact 4-byte header then payload)
static inline int recv_framed(SOCKET sd, char* buffer, int max_len) {
    uint32_t net_len = 0;
    int total_read = 0;

    while (total_read < (int)sizeof(net_len)) {
        int r = recv(sd, ((char*)&net_len) + total_read, sizeof(net_len) - total_read, 0);
        if (r <= 0) return r;
        total_read += r;
    }

    uint32_t payload_len = ntohl(net_len);
    if (payload_len >= (uint32_t)max_len) return -1; // Overflow protection

    total_read = 0;
    while (total_read < (int)payload_len) {
        int r = recv(sd, buffer + total_read, payload_len - total_read, 0);
        if (r <= 0) return r;
        total_read += r;
    }
    buffer[payload_len] = '\0';
    return (int)payload_len;
}

#endif // PROTOCOL_H