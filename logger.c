#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include "logger.h"
#include "common.h"

static FILE           *g_logfile = NULL;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ANSI colour codes for terminal */
static const char *LEVEL_COLOR[] = {
    "\033[0;37m",    /* INFO    — white  */
    "\033[1;33m",    /* WARN    — yellow */
    "\033[1;31m",    /* ERROR   — red    */
    "\033[1;32m"     /* SUCCESS — green  */
};
static const char *LEVEL_NAME[] = { "INFO", "WARN", "ERROR", "OK" };
#define RESET "\033[0m"

void logger_init(const char *log_file) {
    pthread_mutex_lock(&g_log_mutex);
    g_logfile = fopen(log_file, "a");
    if (!g_logfile) {
        fprintf(stderr, "[Logger] Cannot open %s — logging to stderr\n", log_file);
        g_logfile = stderr;
    }
    char ts[32]; get_timestamp(ts, sizeof ts);
    fprintf(g_logfile, "\n=== Session started: %s ===\n", ts);
    fflush(g_logfile);
    pthread_mutex_unlock(&g_log_mutex);
}

void logger_close(void) {
    pthread_mutex_lock(&g_log_mutex);
    if (g_logfile && g_logfile != stderr) {
        char ts[32]; get_timestamp(ts, sizeof ts);
        fprintf(g_logfile, "=== Session ended: %s ===\n\n", ts);
        fclose(g_logfile);
        g_logfile = NULL;
    }
    pthread_mutex_unlock(&g_log_mutex);
}

void log_msg(int level, const char *component,
             const char *client_ip, const char *fmt, ...) {
    char ts[32];
    get_timestamp(ts, sizeof ts);

    pthread_t tid = pthread_self();

    /* Format the message */
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);

    const char *ip = (client_ip && client_ip[0]) ? client_ip : "-";

    pthread_mutex_lock(&g_log_mutex);

    /* Terminal output (coloured) */
    printf("%s[%s] [%s] [tid:%lu] [%s] %s%s\n",
           LEVEL_COLOR[level],
           ts, LEVEL_NAME[level],
           (unsigned long)tid,
           ip,
           msg, RESET);
    fflush(stdout);

    /* File output (plain) */
    if (g_logfile) {
        fprintf(g_logfile, "[%s] [%s] [%s] [tid:%lu] [%s] %s\n",
                ts, component, LEVEL_NAME[level],
                (unsigned long)tid, ip, msg);
        fflush(g_logfile);
    }

    pthread_mutex_unlock(&g_log_mutex);
}
