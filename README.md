# Project 3 — Distributed Software Update Framework
**Course:** ENCS4330 — Real-Time Applications & Embedded Systems
**University:** Birzeit University — Faculty of Engineering and Technology
**Instructors:** Dr. Ahmad Afaneh, Dr. Hanna Bullata

---

## Overview

A distributed client/server software update system using TCP sockets and
POSIX threads. The server listens for client connections, compares software
versions, and transfers update packages to outdated clients. Multiple clients
are handled simultaneously — each in its own thread — so no client ever
waits for another.

---

## How It Works

```
Client connects → sends version number → Server compares with latest
    ├── Client is outdated → Server streams the update file
    │   Client downloads it → saves to disk → simulates installation
    └── Client is current  → Server replies "already up to date"
                             Client closes connection gracefully
```

### Protocol Flow

```
1. Client  →  Server :  NetMessage { MSG_VERSION_REQUEST, my_version }
2. Server  →  Client :  NetMessage { MSG_UPDATE_AVAILABLE, latest_version, file_size }
                     OR NetMessage { MSG_UP_TO_DATE }
3. Server  →  Client :  raw file bytes (streamed in 64KB chunks)
4. Client saves file and closes connection
```

---

## Project Structure

```
project3/
├── server.c            Server main — accept loop, one pthread per client
├── client.c            Client — CheckForUpdate(), getCurrentVersion()
├── common.h            Shared protocol definitions, NetMessage struct
├── common.c            STATUS_NAME string array
├── config.h            ServerConfig and ClientConfig struct definitions
├── config.c            Config file reader for both server and client
├── logger.h            Thread-safe logger declaration
├── logger.c            Logger — timestamps + thread ID on every entry
├── shared_state.h      In-process shared state between server and display
├── display.h           Display thread declaration
├── display.c           OpenGL server dashboard
├── server_config.txt   Server configuration (edit this to change behaviour)
├── client_config.txt   Client configuration (edit this to change behaviour)
└── Makefile            Builds two executables: server and client
```

---

## Two Executables

### `server` — Update Server
- Listens on a TCP port
- Accepts connections from any number of clients
- Creates one POSIX thread per client — clients never block each other
- Compares client version against latest version in config
- Streams the update file if client is outdated
- Shows live dashboard in an OpenGL window

### `client` — Client Application
- Calls `CheckForUpdate()` as required by the assignment
- Calls `getCurrentVersion()` to get current installed version
- Connects to the server, sends version, waits for response
- Downloads and saves the update file if available
- Shows a progress bar during download
- Simulates applying the update after download

---

## Threading Model

```
server process
├── Main thread: accept() loop — waits for new connections
├── Display thread: glutMainLoop() — OpenGL dashboard (pthread)
└── Per-client threads (one created per connection):
    ├── handle_client() for Client 1  (pthread)
    ├── handle_client() for Client 2  (pthread)
    ├── handle_client() for Client 3  (pthread)
    └── ... up to max_clients
```

All threads share `ServerState g_state` (global struct protected by `pthread_mutex_t`).
The display thread reads state every 16ms. Client threads write under the mutex.

---

## IPC / Synchronization

| Mechanism | Purpose |
|-----------|---------|
| **TCP sockets** | Communication between server and client over the network |
| **POSIX threads** | One thread per client — simultaneous handling |
| **pthread_mutex_t** | Protects the shared `ServerState` struct from race conditions |
| **Global shared struct** | In-process state shared between server threads and display thread |

### Why TCP and not UDP?
TCP guarantees ordered, reliable, complete delivery. File transfers require every
byte to arrive in the correct order. UDP would require manual retransmit logic,
sequencing, and reassembly — unnecessary complexity for this task.

### Why one thread per client?
Simple, clean, and directly satisfies the assignment requirement that no client
waits for another. Each `handle_client()` thread is fully independent — one slow
download has zero effect on any other client.

---

## NetMessage Protocol Struct

```c
typedef struct {
    uint32_t type;          // MSG_VERSION_REQUEST / MSG_UPDATE_AVAILABLE / etc.
    uint32_t version;       // client version or latest server version
    uint64_t file_size;     // size of update file (bytes)
    char     client_id[32]; // client hostname or name
    char     info[128];     // human-readable description
} NetMessage;
```

Both sides know `sizeof(NetMessage)` exactly, so there is no delimiter parsing
or string splitting needed — both sides just `send()`/`recv()` the whole struct.

---

## Server Configuration (`server_config.txt`)

```
port            = 9000              # TCP port to listen on
latest_version  = 3                 # newest available software version
update_file     = update_package.bin # path to the update file
max_clients     = 32                # maximum simultaneous connections
log_file        = server.log        # where to write server events
bind_ip         = 0.0.0.0           # listen on all network interfaces
```

## Client Configuration (`client_config.txt`)

```
server_ip       = 127.0.0.1         # server IP address
server_port     = 9000              # server port
current_version = 1                 # currently installed version
client_name     = MyApp-Client      # name shown in logs
save_dir        = ./downloads       # where to save downloaded updates
log_file        = client.log        # where to write client events
```

---

## How to Build and Run

### Install dependencies
```bash
sudo apt-get install -y gcc make freeglut3-dev
```

### Build both executables
```bash
make
```

### Run the server (Terminal 1)
```bash
./server server_config.txt
```

### Run a client (Terminal 2)
```bash
./client client_config.txt
```

### Clean
```bash
make clean
```

---

## Testing

### Test 1 — Single client, outdated version
```bash
# client_config.txt: current_version = 1
./client client_config.txt
```

### Test 2 — Single client, already up to date
```bash
# client_config.txt: current_version = 3
./client client_config.txt
```

### Test 3 — Multiple simultaneous clients
```bash
./client client_config.txt &
./client client_config.txt &
./client client_config.txt &
./client client_config.txt &
wait
```

### Test 4 — Mix of outdated and current clients
```bash
sed 's/current_version.*= 1/current_version = 3/' client_config.txt > /tmp/cfg_v3.txt
./client client_config.txt & ./client /tmp/cfg_v3.txt &
./client client_config.txt & ./client /tmp/cfg_v3.txt &
wait
```

### Test 5 — Interrupted connection
```bash
dd if=/dev/zero of=update_package.bin bs=1M count=50
./client client_config.txt &
CLIENT_PID=$!
sleep 2
kill $CLIENT_PID
wait
```

### Test 6 — Large file transfer
```bash
# Uses the 50MB file from test 5
./client client_config.txt
```

### Test 7 — Invalid request
```bash
telnet localhost 9000
# type: HELLO GARBAGE REQUEST
# press Ctrl+] then type: quit
```

### Test 8 — Stress test (10 simultaneous clients)
```bash
for i in $(seq 1 10); do ./client client_config.txt & done
wait
```

### Check results after all tests
```bash
cat server.log        # all events with timestamps and thread IDs
ls -lh downloads/     # verify downloaded files were saved
```

---

## OpenGL Dashboard (server window)

The server shows a live dashboard:
- **Header:** server name, port, latest version, ONLINE/OFFLINE indicator
- **Stats bar:** active clients, total connections, updates sent, up-to-date count
- **Client rows:** one row per connected client showing:
  - IP address and port
  - Client version (v1, v2, v3...)
  - Status with colour (CONNECTING / CHECKING / DOWNLOADING / DONE / UP-TO-DATE / ERROR)
  - Live progress bar during download
  - Elapsed time
  - Thread ID
- **Log panel:** last 8 events shown at the bottom

---

## Log Files

### `server.log` — example entries
```
[2026-06-04 15:03:20] [Server] [OK]    [tid:128584706500416] [-]        Server listening on port 9000
[2026-06-04 15:03:20] [Server] [INFO]  [tid:128584706500416] [127.0.0.1] New connection from port 54644
[2026-06-04 15:03:20] [Server] [INFO]  [tid:128583536211648] [127.0.0.1] Client version: 1, latest: 3
[2026-06-04 15:03:24] [Server] [OK]    [tid:128583536211648] [127.0.0.1] Transfer complete: 52428800 bytes
```

### `client.log` — example entries
```
[2026-06-04 15:03:20] [Client] [OK]   [tid:139646011283264] [-]              Client application starting
[2026-06-04 15:03:20] [Client] [INFO] [tid:139646011283264] [nsw-VirtualBox] Connecting to 127.0.0.1:9000
[2026-06-04 15:03:20] [Client] [OK]   [tid:139646011283264] [nsw-VirtualBox] Connected to 127.0.0.1:9000
[2026-06-04 15:03:24] [Client] [OK]   [tid:139646011283264] [nsw-VirtualBox] Download complete: 52428800 bytes
```

---

## Features Implemented

| Feature | Status |
|---------|--------|
| `CheckForUpdate()` function | ✅ Required |
| `getCurrentVersion()` function | ✅ Required |
| One pthread per client | ✅ Required |
| Multiple simultaneous clients | ✅ Required |
| File transfer with save to disk | ✅ Required |
| Config files (no hardcoded values) | ✅ Required |
| Timestamped logging with thread ID | ✅ Required |
| Error handling (no crashes) | ✅ Required |
| OpenGL dashboard | ✅ Optional |
| Live download progress bar | ✅ Optional |
| Performance statistics | ✅ Optional |
