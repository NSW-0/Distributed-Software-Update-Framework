#ifndef LOGGER_H
#define LOGGER_H

#include <pthread.h>


void logger_init(const char *log_file);
void logger_close(void);

/* log levels */
#define LOG_INFO    0
#define LOG_WARN    1
#define LOG_ERROR   2
#define LOG_SUCCESS 3

void log_msg(int level, const char *component,
             const char *client_ip, const char *fmt, ...);

/* convenience macros */
#define LOG_I(comp, ip, ...) log_msg(LOG_INFO,    comp, ip, __VA_ARGS__)
#define LOG_W(comp, ip, ...) log_msg(LOG_WARN,    comp, ip, __VA_ARGS__)
#define LOG_E(comp, ip, ...) log_msg(LOG_ERROR,   comp, ip, __VA_ARGS__)
#define LOG_S(comp, ip, ...) log_msg(LOG_SUCCESS, comp, ip, __VA_ARGS__)

#endif
