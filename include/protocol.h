#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
    #include <winsock2.h>
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    typedef int SOCKET;
#endif

#define maxTypeLen 16
#define maxNameLen 32
#define maxPayloadLen 1024
#define maxFrameLen 1120

typedef struct {
    char type[maxTypeLen];       // REGISTER, CHAT, PRIVMSG, JOIN_ROOM, SYSTEM, ERROR
    char sender[maxNameLen];     // Username of sender
    char target[maxNameLen];     // Target room name or target username
    char payload[maxPayloadLen]; // Message content
} MessageFrame;

// Serialize: Convert MessageFrame struct into wire-format string ("TYPE|FROM|TO|PAYLOAD")
static inline void serializeFrame(const MessageFrame* frame, char* out_buffer, size_t max_len) {
    snprintf(out_buffer, max_len, "%s|%s|%s|%s", frame->type, frame->sender, frame->target, frame->payload);
}

// Deserialize: Parse wire-format string ("TYPE|FROM|TO|PAYLOAD") into MessageFrame struct
static inline int deserializeFrame(const char* inBuffer, MessageFrame* frame) {
    memset(frame, 0, sizeof(MessageFrame));
    
    int fields = sscanf(inBuffer, "%15[^|]|%31[^|]|%31[^|]|%1023[^\n]", 
                        frame->type, frame->sender, frame->target, frame->payload);
    
    if (fields >= 3) {
        return 1;
    }
    return 0;
}

// PHASE 9: Send framed message over TCP (4-byte length prefix header + payload)
static inline int send_framed(SOCKET sd, const char* buffer, int len) {
    uint32_t net_len = htonl((uint32_t)len);
    // 1. Send 4-byte length header
    if (send(sd, (const char*)&net_len, sizeof(net_len), 0) <= 0) return -1;
    // 2. Send payload
    if (send(sd, buffer, len, 0) <= 0) return -1;
    return 0;
}

// PHASE 9: Receive framed message over TCP (reads exact 4-byte header then exact payload)
static inline int recv_framed(SOCKET sd, char* buffer, int max_len) {
    uint32_t net_len = 0;
    int total_read = 0;
    
    // 1. Read exact 4-byte length header
    while (total_read < (int)sizeof(net_len)) {
        int r = recv(sd, ((char*)&net_len) + total_read, sizeof(net_len) - total_read, 0);
        if (r <= 0) return r; // Connection closed or error
        total_read += r;
    }
    
    uint32_t payload_len = ntohl(net_len);
    if (payload_len >= (uint32_t)max_len) return -1; // Overflow protection

    // 2. Read exact payload_len bytes
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