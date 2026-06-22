#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <time.h>

/* ── Message types ───────────────────────────────────────── */
#define MSG_VERSION_REQUEST  1   /* client → server: my version */
#define MSG_UPDATE_AVAILABLE 2   /* server → client: update exists */
#define MSG_UP_TO_DATE       3   /* server → client: no update needed */
#define MSG_ERROR            4   /* either direction: error */
#define MSG_FILE_BEGIN       5   /* server → client: file transfer starting */
#define MSG_FILE_END         6   /* server → client: file transfer done */

/* ── Client status codes (for display/logging) ───────────── */
#define STATUS_CONNECTING    0
#define STATUS_CHECKING      1
#define STATUS_DOWNLOADING   2
#define STATUS_DONE          3   /* got an update */
#define STATUS_UP_TO_DATE    4   /* already current */
#define STATUS_ERROR         5
#define STATUS_DISCONNECTED  6

extern const char *STATUS_NAME[];

typedef struct {
    uint32_t type;            /* one of MSG_* above            */
    uint32_t version;         /* client version or latest ver  */
    uint64_t file_size;       /* size of update file (if any)  */
    char     client_id[32];   /* client hostname or identifier */
    char     info[128];       /* human-readable description    */
} NetMessage;

/* ── File transfer chunk size ────────────────────────────── */
#define CHUNK_SIZE   (64 * 1024)   /* 64 KB per send() call */

/* ── Misc ────────────────────────────────────────────────── */
#define MAX_CLIENTS         32
#define UPDATE_FILENAME     "update_package.bin"
#define DEFAULT_SERVER_PORT 9000

/* ── Receive exactly n bytes (handles partial reads) ─────── */
#include <sys/types.h>
#include <sys/socket.h>
static inline ssize_t recv_all(int fd, void *buf, size_t n) {
    size_t total = 0;
    char  *p     = (char *)buf;
    while (total < n) {
        ssize_t r = recv(fd, p + total, n - total, 0);
        if (r <= 0) return r;
        total += (size_t)r;
    }
    return (ssize_t)total;
}

/* ── Send exactly n bytes ────────────────────────────────── */
static inline ssize_t send_all(int fd, const void *buf, size_t n) {
    size_t total = 0;
    const char *p = (const char *)buf;
    while (total < n) {
        ssize_t s = send(fd, p + total, n - total, 0);
        if (s <= 0) return s;
        total += (size_t)s;
    }
    return (ssize_t)total;
}

/* ── Timestamp helper ────────────────────────────────────── */
static inline void get_timestamp(char *buf, size_t len) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buf, len, "%Y-%m-%d %H:%M:%S", t);
}

#endif /* COMMON_H */
