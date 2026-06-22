#ifndef SHARED_STATE_H
#define SHARED_STATE_H


#include <pthread.h>
#include <time.h>
#include "common.h"

typedef struct {
    int    slot;                /* index in clients[] array     */
    char   ip[32];              /* client IP address            */
    int    port;                /* client port                  */
    int    client_version;      /* version the client sent      */
    int    status;              /* STATUS_* from common.h       */
    long   file_size;           /* total bytes to transfer      */
    long   bytes_sent;          /* progress counter             */
    time_t connect_time;        /* when this client connected   */
    time_t finish_time;         /* when done (0 if ongoing)     */
    pthread_t thread_id;        /* which thread handles this    */
    int    active;              /* 1 = slot in use              */
} ClientSlot;

#define LOG_DISPLAY_LINES  8    /* last N log lines shown in display */
#define LOG_LINE_LEN      80

typedef struct {
    /* server info */
    int        running;
    int        port;
    int        latest_version;
    char       update_file[256];

    /* stats */
    int        total_connected;    /* all-time count */
    int        updates_sent;       /* how many got an update */
    int        up_to_date_count;   /* how many were current */
    int        active_count;       /* currently connected */

    /* per-client records */
    ClientSlot clients[MAX_CLIENTS];

    /* recent log lines for display panel */
    char       log_lines[LOG_DISPLAY_LINES][LOG_LINE_LEN];
    int        log_head;           /* ring buffer head */

    pthread_mutex_t mutex;
} ServerState;

/* Global — defined in server.c, used by display.c */
extern ServerState g_state;

/* Helpers */
int  state_add_client(const char *ip, int port, pthread_t tid);
void state_set_status(int slot, int status);
void state_set_progress(int slot, long file_size, long bytes_sent);
void state_finish_client(int slot, int status);
void state_push_log(const char *line);
void state_set_version(int slot, int ver);

#endif
