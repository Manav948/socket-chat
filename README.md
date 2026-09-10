# C TCP Socket Chat Application

A high-performance, non-blocking multi-client C socket chat application featuring a custom wire-framing protocol, multi-threaded client UI, dynamic room channels, private messaging, rate-limiting, and server logging.

---

## System Architecture

```text
               ┌─────────────────────────────────────────┐
               │            TCP CHAT SERVER              │
               │                                         │
               │   ┌─────────────────────────────────┐   │
               │   │    select() I/O Multiplexer     │   │
               │   └────────────────┬────────────────┘   │
               │                    │                    │
               │   ┌────────────────┴────────────────┐   │
               │   │ Application Protocol Parser &   │   │
               │   │     Room Isolation Router       │   │
               │   └────────────────┬────────────────┘   │
               │                    │                    │
               │   ┌────────────────┴────────────────┐   │
               │   │ Rate Limiter & server.log Audit │   │
               │   └─────────────────────────────────┘   │
               └────────────┬──────────┬──────────┬──────┘
                            │          │          │
             ┌──────────────┘          │          └──────────────┐
             │                         │                         │
             ▼                         ▼                         ▼
   ┌──────────────────┐      ┌──────────────────┐      ┌──────────────────┐
   │     Client 1     │      │     Client 2     │      │     Client 3     │
   │  User: "Manav"   │      │  User: "Rahul"   │      │  User: "Kavay"   │
   │  Room: #general  │      │  Room: #general  │      │  Room: #gaming   │
   └──────────────────┘      └──────────────────┘      └──────────────────┘
```

---

## Client Architecture

```text
   ┌──────────────────────────────────────────────────────────────┐
   │                     CLIENT PROCESS                           │
   │                                                              │
   │   ┌────────────────────────┐    ┌────────────────────────┐   │
   │   │      MAIN THREAD       │    │     ASYNC RX THREAD    │   │
   │   │                        │    │                        │   │
   │   │  • Reads stdin [YOU]:  │    │  • Listens on Socket   │   │
   │   │  • Serializes Frame    │    │  • Reads 4-Byte Header │   │
   │   │  • Sends to TCP Socket │    │  • Formats Chat Output │   │
   │   └───────────┬────────────┘    └───────────▲────────────┘   │
   └───────────────┼─────────────────────────────┼────────────────┘
                   │                             │
                   ▼                             │
   ┌─────────────────────────────────────────────┴────────────────┐
   │                     TCP WIRE TRANSPORT                       │
   │           [ 4-Byte Length Header ] + [ Payload Frame ]       │
   └──────────────────────────────────────────────────────────────┘
```

---

## Client-Server Workflow

```text
┌──────────┐               ┌────────────┐               ┌──────────┐
│  Client  │               │ TCP Socket │               │  Server  │
└────┬─────┘               └─────┬──────┘               └────┬─────┘
     │                           │                           │
     │── Connect (IP:Port) ─────►│── 3-Way Handshake ───────►│
     │                           │                           │
     │── REGISTER|Manav ────────►│── send_framed() ─────────►│── Register Session
     │                           │                           │── Audit server.log
     │◄── SYSTEM|Welcome ────────│◄── send_framed() ─────────│
     │                           │                           │
     │── JOIN_ROOM|#dev ────────►│── send_framed() ─────────►│── Switch Channel
     │◄── Joined #dev ───────────│◄── Broadcast ─────────────│
     │                           │                           │
     │── CHAT|#dev|Hello! ──────►│── send_framed() ─────────►│── Rate-Limit Check
     │◄── Manav: Hello! ─────────│◄── Multi-cast Broadcast ──│
     │                           │                           │
     │── PRIVMSG|Rahul|Hey ─────►│── send_framed() ─────────►│── Lookup Target User
     │                           │   (Target Rahul only) ────►│── Deliver PM
     │                           │                           │
     │── /quit ─────────────────►│── Close Socket ──────────►│── Disconnect & Cleanup
```

---

## Wire Protocol Framing

TCP is a streaming protocol without native frame boundaries. Every message uses a **4-byte Big-Endian Length-Prefix Header** followed by a pipe-delimited payload (`TYPE|SENDER|TARGET|PAYLOAD`).

```text
┌─────────────────────────────────────────────────────────────────────────┐
│              Payload Length (4 Bytes - Big-Endian Header)               │
├───────────────────┬───────────────────┬───────────────────┬─────────────┤
│  TYPE (max 16B)   │ SENDER (max 32B)  │ TARGET (max 32B)  │ PAYLOAD     │
│ ("CHAT", "PRIV")  │ ("Manav", etc.)   │ ("#dev", etc.)    │ (max 1024B) │
└───────────────────┴───────────────────┴───────────────────┴─────────────┘
```

---

## Core Features

- **Non-Blocking I/O**: `select()` multiplexing manages multiple clients concurrently on a single master loop.
- **Frame Demarcation**: 4-byte length prefix prevents TCP packet fragmentation and concatenation.
- **Multi-Threaded Client**: Asynchronous receive thread ensures uninterrupted terminal UI (`[YOU]:`).
- **Room Channels & Private DMs**: Multi-cast room channels (`/join`) and targeted private chats (`/msg`).
- **Rate-Limiting Throttling**: Sliding window caps user message bursts to 5 messages per 2 seconds.
- **Audit Logging**: Background file logging records server events and messages to `server.log`.

---

## Command Reference

| Command | Description | Example |
| :--- | :--- | :--- |
| `/join <room>` | Switch or create chat room channel | `/join #dev` |
| `/msg <user> <msg>` | Send private message to user | `/msg alice hello` |
| `/rooms` | List active chat rooms | `/rooms` |
| `/help` | Show supported commands | `/help` |
| `/quit` | Disconnect cleanly | `/quit` |

---

## Quick Start Guide

### 1. Compilation

**Windows (MinGW):**
```bash
gcc -Wall -O2 server/server.c -o server/server.exe -lws2_32
gcc -Wall -O2 client/client.c -o client/client.exe -lws2_32
```

**Linux / macOS (POSIX):**
```bash
gcc -Wall -O2 server/server.c -o server/server
gcc -Wall -O2 client/client.c -o client/client -lpthread
```

---

### 2. Execution

**Start Server:**
```bash
./server/server.exe 8888
```

**Start Client:**
```bash
./client/client.exe 127.0.0.1 8888
```
