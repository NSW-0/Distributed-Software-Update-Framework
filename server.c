#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/stat.h>
#include <signal.h>

#include "common.h"
#include "config.h"
#include "logger.h"
#include "shared_state.h"
#include "display.h"

/* ── Global state (shared with display.c) ────────────────── */
ServerState g_state;

/* ── Config (set at startup, read-only after) ─────────────── */
static ServerConfig g_cfg;

/* ── Accept socket ────────────────────────────────────────── */
static int g_server_fd = -1;

/* ─────────────────────────────────────────────────────────── */
/*  shared_state helpers                                       */
/* ─────────────────────────────────────────────────────────── */

int state_add_client(const char *ip, int port, pthread_t tid) {
    pthread_mutex_lock(&g_state.mutex);
    int slot = -1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!g_state.clients[i].active) { slot = i; break; }
    }
    if (slot >= 0) {
        ClientSlot *c = &g_state.clients[slot];
        memset(c, 0, sizeof *c);
        c->slot         = slot;
        strncpy(c->ip, ip, sizeof c->ip - 1);
        c->port         = port;
        c->status       = STATUS_CONNECTING;
        c->connect_time = time(NULL);
        c->thread_id    = tid;
        c->active       = 1;
        g_state.total_connected++;
        g_state.active_count++;
    }
    pthread_mutex_unlock(&g_state.mutex);
    return slot;
}

void state_set_status(int slot, int status) {
    if (slot < 0) return;
    pthread_mutex_lock(&g_state.mutex);
    g_state.clients[slot].status = status;
    pthread_mutex_unlock(&g_state.mutex);
}

void state_set_version(int slot, int ver) {
    if (slot < 0) return;
    pthread_mutex_lock(&g_state.mutex);
    g_state.clients[slot].client_version = ver;
    pthread_mutex_unlock(&g_state.mutex);
}

void state_set_progress(int slot, long file_size, long bytes_sent) {
    if (slot < 0) return;
    pthread_mutex_lock(&g_state.mutex);
    g_state.clients[slot].file_size  = file_size;
    g_state.clients[slot].bytes_sent = bytes_sent;
    pthread_mutex_unlock(&g_state.mutex);
}

void state_finish_client(int slot, int status) {
    if (slot < 0) return;
    pthread_mutex_lock(&g_state.mutex);
    g_state.clients[slot].status      = status;
    g_state.clients[slot].finish_time = time(NULL);
    g_state.active_count--;
    if (status == STATUS_DONE)        g_state.updates_sent++;
    if (status == STATUS_UP_TO_DATE)  g_state.up_to_date_count++;
    pthread_mutex_unlock(&g_state.mutex);
}

void state_push_log(const char *line) {
    pthread_mutex_lock(&g_state.mutex);
    int h = g_state.log_head;
    strncpy(g_state.log_lines[h], line, LOG_LINE_LEN - 1);
    g_state.log_head = (h + 1) % LOG_DISPLAY_LINES;
    pthread_mutex_unlock(&g_state.mutex);
}

/* ─────────────────────────────────────────────────────────── */
/*  Generate a fake update package if it doesn't exist        */
/* ─────────────────────────────────────────────────────────── */
static void ensure_update_file(const char *path, int version) {
    struct stat st;
    if (stat(path, &st) == 0) return;   /* already exists */

    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "[Server] Cannot create %s\n", path); return; }

    /* Write a realistic-looking package: header + data */
    char header[256];
    snprintf(header, sizeof header,
             "=== SOFTWARE UPDATE PACKAGE ===\n"
             "Version : %d\n"
             "Built   : %s\n"
             "Vendor  : ENCS4330 Lab\n"
             "================================\n",
             version, __DATE__);
    fwrite(header, 1, strlen(header), f);

    /* Pad to ~2 MB so transfer is visible */
    char chunk[4096];
    memset(chunk, 'A', sizeof chunk);
    for (int i = 0; i < 512; i++) fwrite(chunk, 1, sizeof chunk, f);

    fclose(f);
    printf("[Server] Created update package: %s\n", path);
}

/* ─────────────────────────────────────────────────────────── */
/*  Per-client handler — runs in its own pthread              */
/* ─────────────────────────────────────────────────────────── */
typedef struct {
    int  socket_fd;
    char client_ip[32];
    int  client_port;
    int  slot;
} ClientArg;

static void *handle_client(void *arg) {
    ClientArg *ca = (ClientArg *)arg;
    int  fd       = ca->socket_fd;
    char ip[32];
    strncpy(ip, ca->client_ip, sizeof ip - 1);
    int slot      = ca->slot;
    free(ca);

    LOG_I("Server", ip, "Thread started, handling connection");
    state_set_status(slot, STATUS_CHECKING);
    state_push_log(ip);   /* quick display update */

    /* ── 1. Receive VERSION_REQUEST ── */
    NetMessage req;
    memset(&req, 0, sizeof req);
    ssize_t r = recv_all(fd, &req, sizeof req);
    if (r <= 0 || req.type != MSG_VERSION_REQUEST) {
        LOG_E("Server", ip, "Failed to receive version request");
        state_finish_client(slot, STATUS_ERROR);
        close(fd);
        return NULL;
    }

    int client_ver = (int)req.version;
    state_set_version(slot, client_ver);

    char logbuf[128];
    snprintf(logbuf, sizeof logbuf, "Client version: %d, latest: %d",
             client_ver, g_cfg.latest_version);
    LOG_I("Server", ip, "%s", logbuf);
    state_push_log(logbuf);

    /* ── 2. Compare versions ── */
    NetMessage resp;
    memset(&resp, 0, sizeof resp);
    resp.version = (uint32_t)g_cfg.latest_version;

    if (client_ver >= g_cfg.latest_version) {
        /* Client is up to date */
        resp.type = MSG_UP_TO_DATE;
        snprintf(resp.info, sizeof resp.info,
                 "Already at version %d — no update needed", client_ver);
        send_all(fd, &resp, sizeof resp);

        LOG_S("Server", ip, "Client is up to date (v%d)", client_ver);
        state_finish_client(slot, STATUS_UP_TO_DATE);
        close(fd);
        return NULL;
    }

    /* ── 3. Client needs update — get file size ── */
    FILE *uf = fopen(g_cfg.update_file, "rb");
    if (!uf) {
        resp.type = MSG_ERROR;
        snprintf(resp.info, sizeof resp.info, "Update file not found on server");
        send_all(fd, &resp, sizeof resp);
        LOG_E("Server", ip, "Cannot open update file %s", g_cfg.update_file);
        state_finish_client(slot, STATUS_ERROR);
        close(fd);
        return NULL;
    }

    fseek(uf, 0, SEEK_END);
    long fsize = ftell(uf);
    fseek(uf, 0, SEEK_SET);

    /* ── 4. Send UPDATE_AVAILABLE with file size ── */
    resp.type      = MSG_UPDATE_AVAILABLE;
    resp.file_size = (uint64_t)fsize;
    snprintf(resp.info, sizeof resp.info,
             "Update available: v%d → v%d  (%ld bytes)",
             client_ver, g_cfg.latest_version, fsize);

    if (send_all(fd, &resp, sizeof resp) < 0) {
        LOG_E("Server", ip, "Failed to send UPDATE_AVAILABLE");
        fclose(uf);
        state_finish_client(slot, STATUS_ERROR);
        close(fd);
        return NULL;
    }

    LOG_I("Server", ip,
          "Sending update v%d → v%d (%ld bytes)",
          client_ver, g_cfg.latest_version, fsize);

    state_set_status(slot, STATUS_DOWNLOADING);
    state_set_progress(slot, fsize, 0);

    /* ── 5. Stream the file in chunks ── */
    char buf[CHUNK_SIZE];
    long bytes_sent = 0;
    size_t n;

    while ((n = fread(buf, 1, sizeof buf, uf)) > 0) {
        if (send_all(fd, buf, n) < (ssize_t)n) {
            LOG_E("Server", ip, "Send failed after %ld bytes", bytes_sent);
            fclose(uf);
            state_finish_client(slot, STATUS_ERROR);
            close(fd);
            return NULL;
        }
        bytes_sent += (long)n;
        state_set_progress(slot, fsize, bytes_sent);
        /* Small delay between chunks — makes transfer measurable on localhost
         * so interrupted-connection test and progress bar are demonstrable.
         * 5ms per 64KB chunk ≈ ~12 MB/s, visible on screen. */
        usleep(5000);
    }
    fclose(uf);

    snprintf(logbuf, sizeof logbuf,
             "Transfer complete: %ld bytes sent to %s", bytes_sent, ip);
    LOG_S("Server", ip, "%s", logbuf);
    state_push_log(logbuf);
    state_finish_client(slot, STATUS_DONE);
    close(fd);
    return NULL;
}

/* ─────────────────────────────────────────────────────────── */
/*  Accept loop — runs in main thread                         */
/* ─────────────────────────────────────────────────────────── */
static void run_accept_loop(void) {
    struct sockaddr_in cli_addr;
    socklen_t cli_len = sizeof cli_addr;

    char logbuf[128];
    snprintf(logbuf, sizeof logbuf, "Server listening on port %d", g_cfg.port);
    LOG_S("Server", "", "%s", logbuf);
    state_push_log(logbuf);

    while (g_state.running) {
        int client_fd = accept(g_server_fd,
                               (struct sockaddr *)&cli_addr, &cli_len);
        if (client_fd < 0) {
            if (errno == EINTR || !g_state.running) break;
            LOG_E("Server", "", "accept() failed: %s", strerror(errno));
            continue;
        }

        char ip[32];
        strncpy(ip, inet_ntoa(cli_addr.sin_addr), sizeof ip - 1);
        int  port = ntohs(cli_addr.sin_port);

        LOG_I("Server", ip, "New connection from port %d", port);

        /* Allocate thread argument */
        ClientArg *ca = malloc(sizeof *ca);
        if (!ca) { close(client_fd); continue; }

        ca->socket_fd   = client_fd;
        strncpy(ca->client_ip, ip, sizeof ca->client_ip - 1);
        ca->client_port = port;


        pthread_t tid = (pthread_t)0;
        ca->slot = state_add_client(ip, port, tid);

        pthread_create(&tid, NULL, handle_client, ca);

        if (ca->slot >= 0) {
            pthread_mutex_lock(&g_state.mutex);
            g_state.clients[ca->slot].thread_id = tid;
            pthread_mutex_unlock(&g_state.mutex);
        }

        pthread_detach(tid);

        snprintf(logbuf, sizeof logbuf, "New client: %s:%d", ip, port);
        state_push_log(logbuf);
    }
}

/* ─────────────────────────────────────────────────────────── */
/*  Signal handler — graceful shutdown                        */
/* ─────────────────────────────────────────────────────────── */
static void sig_handler(int sig) {
    (void)sig;
    printf("\n[Server] Shutdown signal received...\n");
    g_state.running = 0;
    if (g_server_fd >= 0) close(g_server_fd);
}

/* ─────────────────────────────────────────────────────────── */
/*  main()                                                     */
/* ─────────────────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <server_config.txt>\n", argv[0]);
        return 1;
    }

    load_server_config(argv[1], &g_cfg);
    print_server_config(&g_cfg);

    /* Init logger */
    logger_init(g_cfg.log_file);
    LOG_S("Server", "", "Update server starting up");

    /* Init shared state */
    memset(&g_state, 0, sizeof g_state);
    pthread_mutex_init(&g_state.mutex, NULL);
    g_state.running        = 1;
    g_state.port           = g_cfg.port;
    g_state.latest_version = g_cfg.latest_version;
    strncpy(g_state.update_file, g_cfg.update_file,
            sizeof g_state.update_file - 1);

    /* Ensure update package exists */
    ensure_update_file(g_cfg.update_file, g_cfg.latest_version);

    /* Create TCP socket */
    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_fd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);
    /* Small send buffer — makes large transfers take a few seconds on localhost
     * so the interrupted-connection test is demonstrable */
    int sndbuf = 64 * 1024;
    setsockopt(g_server_fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof sndbuf);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t)g_cfg.port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(g_server_fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("bind"); close(g_server_fd); return 1;
    }
    if (listen(g_server_fd, g_cfg.max_clients) < 0) {
        perror("listen"); close(g_server_fd); return 1;
    }

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGPIPE, SIG_IGN);

    /* Start OpenGL display in a background thread.
     * If display fails (no X server / GLX), the server continues normally. */
    pthread_t display_tid;
    pthread_attr_t dattr;
    pthread_attr_init(&dattr);
    pthread_attr_setdetachstate(&dattr, PTHREAD_CREATE_DETACHED);
    pthread_create(&display_tid, &dattr, display_thread, NULL);
    pthread_attr_destroy(&dattr);

    /* Run accept loop (blocks until shutdown) */
    run_accept_loop();

    /* Cleanup */
    LOG_I("Server", "", "Server shutting down");
    sleep(1);   /* let in-flight threads log final messages */
    logger_close();
    pthread_mutex_destroy(&g_state.mutex);
    printf("[Server] Goodbye.\n");
    return 0;
}
