#  Socket chat is a Real-Time Multi-Client TCP Chat Application in C

A production-grade, low-level real-time chat application built from scratch using **C** and **POSIX / Winsock TCP socket programming**. 

This project covers computer networking fundamentals—from kernel socket buffers and the TCP 3-way handshake up to application-layer protocol design, message framing, rate-limiting security, and multi-threaded client execution.

---

##  Key Features

- **🚀 I/O Multiplexing (`select()`)**: Handles up to 30 concurrent client connections on a single server thread without spawning expensive OS threads per client.
- **📦 Custom Application Protocol**: Structured pipe-delimited message frames (`TYPE|FROM|TO|PAYLOAD`) for predictable, type-safe network parsing.
- **📏 Message Framing (Length-Prefixing)**: 4-byte big-endian length headers (`send_framed` / `recv_framed`) resolving **TCP Sticky Packets** and **Packet Fragmentation**.
- **🏠 Room-Scoped Isolation (Channels)**: Join different rooms (`#general`, `#gaming`, `#coding`). Broadcast messages are isolated to users in the same channel.
- **🔒 Private Messaging (`/msg`)**: Direct point-to-point messaging between users.
- **⚡ Multi-Threaded CLI Client**: Asynchronous background receive thread (`CreateThread` / `pthread`) rendering incoming messages instantly while typing.
- **🛡️ Security Guard & Rate Limiting**: Built-in rate limiter (max 5 msgs / 2 seconds) and input sanitization preventing header injection (`|`).
- **📜 Server Observability & File Logging**: Persistent, timestamped event logging saved automatically to `server.log`.
- **🛑 Graceful Shutdown (`SIGINT` Handler)**: Catches `Ctrl+C` signal to notify clients and cleanly close socket descriptors.

---

##  System Architecture

### 1. High-Level Client-Server Architecture

```text
               ┌─────────────────────────────────────────┐
               │            TCP CHAT SERVER              │
               │                                         │
               │   ┌─────────────────────────────────┐   │
               │   │   select() I/O Multiplexing     │   │
               │   └────────────────┬────────────────┘   │
               │                    │                    │
               │   ┌────────────────┴────────────────┐   │
               │   │ Application Protocol Parser &   │   │
               │   │     Room Isolation Router       │   │
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

### 2. Message Framing Architecture (Phase 9)

TCP is a continuous byte-stream protocol. To prevent packet coalescing (sticky packets) and fragmentation, every message is prefixed with a **4-Byte Big-Endian Length Header**:

```text
┌───────────────────────────────────────┬─────────────────────────────────────────────────┐
│  4-Byte Length Header (Network Order) │             Serialized Protocol Frame           │
│  [ uint32_t payload_len ]             │      "TYPE | SENDER | TARGET | PAYLOAD"         │
└───────────────────────────────────────┴─────────────────────────────────────────────────┘
│ <─────────── 4 Bytes ───────────────> │ <────────────── payload_len Bytes ────────────> │
```

---

### 3. Client State Machine

```text
[TCP Handshake Established]
            │
            ▼
    State: Unregistered
            │
            ├─── User inputs Username
            │    ├── [Invalid / Taken] ──> Prompt retry
            │    └── [Valid] ───────────> Transition to Registered
            ▼
    State: Registered (#general)
            │
            ├─── /join <room> ──────────> Switch Room Channel
            ├─── /msg <user> <text> ────> Private Message Router
            ├─── <text message> ────────> Room Broadcast
            └─── /quit ─────────────────> Graceful Disconnect
```

---

##  Project Directory Structure

```text
tcp-chat-app/
├── protocol.h            # Protocol headers, framing (send_framed/recv_framed), & serializer
├── server/
│   ├── server.c          # Multi-client TCP server (select(), rooms, logging, rate limiting)
│   ├── server.exe        # Compiled server binary (Windows)
│   └── server.log        # Timestamped server log file
└── client/
    ├── client.c          # Multi-threaded CLI chat client (CreateThread, local [YOU] prompt)
    └── client.exe        # Compiled client binary (Windows)
```

---

##  Command Reference

Inside the client interactive prompt, the following commands are available:

| Command | Action | Example |
| :--- | :--- | :--- |
| **`<message>`** | Broadcast message to all users in current room | `Hello everyone!` |
| **`/join <room>`** | Switch active chat room / channel | `/join gaming` |
| **`/leave`** | Return to default `#general` room | `/leave` |
| **`/rooms`** | Display all active rooms & online user counts | `/rooms` |
| **`/msg <user> <msg>`** | Send a private message to a specific user | `/msg Rahul Hey, secret!` |
| **`/quit`** | Gracefully disconnect from server | `/quit` |

---

## 🛠️ Compilation & Execution Guide

### Prerequisites
- **Compiler**: GCC (MinGW for Windows, or standard GCC for Linux/macOS)
- **Library**: `ws2_32` (Windows Sockets 2)

### 1. Compiling the Application

#### Windows (MinGW PowerShell):
```powershell
# Compile Server
gcc server/server.c -o server/server.exe -lws2_32

# Compile Client
gcc client/client.c -o client/client.exe -lws2_32
```

#### Linux / macOS (GCC POSIX):
```bash
# Compile Server
gcc server/server.c -o server/server

# Compile Client
gcc client/client.c -o client/client -lpthread
```

---

### 2. Running the Application

1. **Start the Server** (Terminal 1):
   ```powershell
   .\server\server.exe
   ```

2. **Start Client 1 - "Manav"** (Terminal 2):
   ```powershell
   .\client\client.exe
   ```
   *Prompt*: Enter username: `Manav`

3. **Start Client 2 - "Rahul"** (Terminal 3):
   ```powershell
   .\client\client.exe
   ```
   *Prompt*: Enter username: `Rahul`

---

## 📋 Project Development Roadmap (All 12 Phases Completed)

- [x] **Phase 1 — TCP Fundamentals**: Low-level socket APIs (`socket`, `bind`, `listen`, `accept`, `connect`, `close`).
- [x] **Phase 2 — Two-Way Communication**: Stream socket data exchange, kernel ring buffers (`SO_SNDBUF`, `SO_RCVBUF`), string null-termination.
- [x] **Phase 3 — Multi-Client Server**: Single-threaded non-blocking I/O multiplexing with `select()`.
- [x] **Phase 4 — User Management**: Logical identity mapping (`Username` $\rightarrow$ `Socket FD`) and registration state machine.
- [x] **Phase 5 — Broadcast Messaging**: Real-time group chat routing & multi-threaded client receive loop (`CreateThread`).
- [x] **Phase 6 — Private Messaging**: Direct point-to-point user routing (`/msg <username> <message>`).
- [x] **Phase 7 — Chat Rooms**: Isolated channel routing (`/join <room>`, `/leave`, `/rooms`).
- [x] **Phase 8 — Custom Application Protocol**: Pipe-delimited wire frame structure (`TYPE|FROM|TO|PAYLOAD`) and custom parser.
- [x] **Phase 9 — Message Framing**: 4-Byte big-endian length-prefix headers (`send_framed` / `recv_framed`) eliminating TCP sticky packets & packet fragmentation.
- [x] **Phase 10 — Server Logging & Observability**: Persistent event logging to disk (`server.log`) with ISO timestamps and log levels.
- [x] **Phase 11 — Reliability & Graceful Error Handling**: Graceful signal handling (`SIGINT` / `Ctrl+C`), resource cleanup, and fault recovery.
- [x] **Phase 12 — Security & System Design**: Rate limiting guard (max 5 msgs / 2 seconds), input sanitization, and buffer overflow protection.

---

##  Deep Networking Concepts Learned

1. **OSI & TCP/IP Model**: Understanding how transport-layer TCP segments wrap application-layer data payload.
2. **Byte Ordering**: Converting host integers (Little-Endian on x86) to Network Byte Order (Big-Endian) via `htons()` and `htonl()`.
3. **Kernel Socket Buffers**: Interacting with OS kernel memory send (`SO_SNDBUF`) and receive (`SO_RCVBUF`) queues.
4. **I/O Multiplexing**: How `select()` uses bitmasks (`fd_set`) to allow a single thread to sleep until activity occurs on ready file descriptors.
5. **TCP Framing**: Why TCP does not preserve message boundaries and how length-prefix framing guarantees atomic application packet reads.
6. **Rate Limiting & Security**: Protecting server memory and network bandwidth against denial-of-service (DoS) spam attacks.

---

##  License
This project is developed for educational and systems programming research purposes. Open source under the MIT License.
