#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/stat.h>

#include "common.h"
#include "config.h"
#include "logger.h"

static ClientConfig g_cfg;

/*
 * getCurrentVersion()
 *
 * Returns the client's currently installed software version.
 * In a real system this would read a registry entry or version file.
 * Here it returns the value from the config file.
 */
int getCurrentVersion(void) {
    return g_cfg.current_version;
}

/*
 * CheckForUpdate()
 * Returns:
 *   0  — already up to date
 *   1  — update downloaded successfully
 *  -1  — error
 */
int CheckForUpdate(void) {
    char client_ip[64];
    gethostname(client_ip, sizeof client_ip);

    LOG_I("Client", client_ip, "CheckForUpdate() called (current version: %d)",
          getCurrentVersion());

    /* ── 1. Create TCP socket ── */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        LOG_E("Client", client_ip, "Cannot create socket: %s", strerror(errno));
        return -1;
    }

    /* ── 2. Connect to server ── */
    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof saddr);
    saddr.sin_family      = AF_INET;
    saddr.sin_port        = htons((uint16_t)g_cfg.server_port);
    if (inet_pton(AF_INET, g_cfg.server_ip, &saddr.sin_addr) <= 0) {
        LOG_E("Client", client_ip, "Invalid server IP: %s", g_cfg.server_ip);
        close(sock);
        return -1;
    }

    LOG_I("Client", client_ip, "Connecting to %s:%d ...",
          g_cfg.server_ip, g_cfg.server_port);

    if (connect(sock, (struct sockaddr *)&saddr, sizeof saddr) < 0) {
        LOG_E("Client", client_ip, "Connection failed: %s", strerror(errno));
        close(sock);
        return -1;
    }

    LOG_S("Client", client_ip, "Connected to %s:%d",
          g_cfg.server_ip, g_cfg.server_port);

    /* ── 3. Send VERSION_REQUEST ── */
    NetMessage req;
    memset(&req, 0, sizeof req);
    req.type    = MSG_VERSION_REQUEST;
    req.version = (uint32_t)getCurrentVersion();
    strncpy(req.client_id, g_cfg.client_name, sizeof req.client_id - 1);
    snprintf(req.info, sizeof req.info,
             "Version check from %s (v%d)", g_cfg.client_name, getCurrentVersion());

    if (send_all(sock, &req, sizeof req) < 0) {
        LOG_E("Client", client_ip, "Failed to send version request");
        close(sock);
        return -1;
    }

    LOG_I("Client", client_ip, "Sent version request: v%d", getCurrentVersion());

    /* ── 4. Receive server response ── */
    NetMessage resp;
    memset(&resp, 0, sizeof resp);
    if (recv_all(sock, &resp, sizeof resp) <= 0) {
        LOG_E("Client", client_ip, "Failed to receive server response");
        close(sock);
        return -1;
    }

    /* ── 5. Handle response ── */
    if (resp.type == MSG_UP_TO_DATE) {
        printf("\n");
        printf("  ✓  Software is up to date! (version %d)\n", getCurrentVersion());
        printf("     %s\n\n", resp.info);
        LOG_S("Client", client_ip, "Already up to date at v%d", getCurrentVersion());
        close(sock);
        return 0;
    }

    if (resp.type == MSG_ERROR) {
        LOG_E("Client", client_ip, "Server error: %s", resp.info);
        close(sock);
        return -1;
    }

    if (resp.type != MSG_UPDATE_AVAILABLE) {
        LOG_E("Client", client_ip, "Unexpected response type: %d", resp.type);
        close(sock);
        return -1;
    }

    /* ── 6. Update available — prepare to download ── */
    long file_size    = (long)resp.file_size;
    int  new_version  = (int)resp.version;

    printf("\n");
    printf("  ↓  Update available: v%d → v%d (%ld bytes)\n",
           getCurrentVersion(), new_version, file_size);
    printf("     %s\n", resp.info);
    printf("     Downloading...\n\n");

    LOG_I("Client", client_ip,
          "Update available: v%d → v%d (%ld bytes)",
          getCurrentVersion(), new_version, file_size);

    /* ── 7. Build save path ── */
    char save_path[512];
    snprintf(save_path, sizeof save_path, "%s/update_v%d.bin",
             g_cfg.save_dir, new_version);

    /* Create save directory if needed */
    struct stat st;
    if (stat(g_cfg.save_dir, &st) != 0)
        mkdir(g_cfg.save_dir, 0755);

    FILE *out = fopen(save_path, "wb");
    if (!out) {
        LOG_E("Client", client_ip, "Cannot open %s for writing: %s",
              save_path, strerror(errno));
        close(sock);
        return -1;
    }

    /* ── 8. Receive file data in chunks ── */
    char   buf[CHUNK_SIZE];
    long   total  = 0;
    int    last_pct = -1;
    ssize_t n;

    while (total < file_size) {
        long remaining = file_size - total;
        size_t want = (remaining < CHUNK_SIZE) ? (size_t)remaining : CHUNK_SIZE;
        n = recv_all(sock, buf, want);
        if (n <= 0) {
            LOG_E("Client", client_ip,
                  "Connection lost after %ld / %ld bytes", total, file_size);
            fclose(out);
            close(sock);
            return -1;
        }
        fwrite(buf, 1, (size_t)n, out);
        total += n;

        /* Progress indicator */
        int pct = (int)((double)total / (double)file_size * 100.0);
        if (pct != last_pct && pct % 10 == 0) {
            printf("     [");
            int filled = pct / 5;
            for (int i=0;i<20;i++) printf(i<filled?"█":"░");
            printf("] %3d%%  (%ld / %ld bytes)\n", pct, total, file_size);
            fflush(stdout);
            last_pct = pct;
        }
    }
    fclose(out);

    printf("\n");
    printf("  ✓  Download complete!\n");
    printf("     Saved to: %s\n", save_path);
    printf("     Size    : %ld bytes\n\n", total);

    LOG_S("Client", client_ip,
          "Download complete: %ld bytes saved to %s", total, save_path);

    /* ── 9. Simulate applying the update ── */
    printf("  ⚙  Applying update (simulation)...\n");
    sleep(1);
    printf("  ✓  Update applied. New version: %d\n\n", new_version);

    LOG_S("Client", client_ip,
          "Update applied (simulated). New version: %d", new_version);

    close(sock);
    return 1;
}

/* ─────────────────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <client_config.txt>\n", argv[0]);
        return 1;
    }

    load_client_config(argv[1], &g_cfg);
    print_client_config(&g_cfg);

    logger_init(g_cfg.log_file);
    LOG_S("Client", "", "Client application starting");

    printf("═══════════════════════════════════════\n");
    printf("   SOFTWARE UPDATE CLIENT\n");
    printf("═══════════════════════════════════════\n\n");

    int result = CheckForUpdate();

    switch (result) {
        case  0: printf("Status: Already up to date.\n");       break;
        case  1: printf("Status: Update completed successfully.\n"); break;
        default: printf("Status: Error during update check.\n"); break;
    }

    logger_close();
    return (result < 0) ? 1 : 0;
}
