#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "common.h"

static void parse_line(const char *line, char *key, char *val) {
    key[0] = val[0] = '\0';
    if (line[0]=='#'||line[0]=='\n'||line[0]=='\r') return;
    int r = sscanf(line, " %127[^=] = %255[^\n\r]", key, val);
    if (r < 2) sscanf(line, " %127[^=]=%255[^\n\r]", key, val);
    /* trim trailing spaces from key */
    char *e = key + strlen(key) - 1;
    while (e > key && *e == ' ') *e-- = '\0';
}

int load_server_config(const char *path, ServerConfig *cfg) {
    /* defaults */
    cfg->port           = DEFAULT_SERVER_PORT;
    cfg->latest_version = 2;
    strncpy(cfg->update_file, UPDATE_FILENAME, sizeof cfg->update_file-1);
    cfg->max_clients    = MAX_CLIENTS;
    strncpy(cfg->log_file,   "server.log",    sizeof cfg->log_file-1);
    strncpy(cfg->bind_ip,    "0.0.0.0",       sizeof cfg->bind_ip-1);

    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr,"[Config] Cannot open %s, using defaults\n",path); return -1; }
    char line[512], key[128], val[256];
    while (fgets(line, sizeof line, f)) {
        parse_line(line, key, val);
        if (!*key) continue;
        if      (!strcmp(key,"port"))           cfg->port           = atoi(val);
        else if (!strcmp(key,"latest_version")) cfg->latest_version = atoi(val);
        else if (!strcmp(key,"update_file"))    strncpy(cfg->update_file, val, sizeof cfg->update_file-1);
        else if (!strcmp(key,"max_clients"))    cfg->max_clients    = atoi(val);
        else if (!strcmp(key,"log_file"))       strncpy(cfg->log_file,    val, sizeof cfg->log_file-1);
        else if (!strcmp(key,"bind_ip"))        strncpy(cfg->bind_ip,     val, sizeof cfg->bind_ip-1);
    }
    fclose(f);
    return 0;
}

int load_client_config(const char *path, ClientConfig *cfg) {
    /* defaults */
    strncpy(cfg->server_ip,   "127.0.0.1",   sizeof cfg->server_ip-1);
    cfg->server_port    = DEFAULT_SERVER_PORT;
    cfg->current_version= 1;
    strncpy(cfg->client_name, "client",       sizeof cfg->client_name-1);
    strncpy(cfg->save_dir,    ".",            sizeof cfg->save_dir-1);
    strncpy(cfg->log_file,    "client.log",  sizeof cfg->log_file-1);

    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr,"[Config] Cannot open %s, using defaults\n",path); return -1; }
    char line[512], key[128], val[256];
    while (fgets(line, sizeof line, f)) {
        parse_line(line, key, val);
        if (!*key) continue;
        if      (!strcmp(key,"server_ip"))      strncpy(cfg->server_ip,   val, sizeof cfg->server_ip-1);
        else if (!strcmp(key,"server_port"))    cfg->server_port    = atoi(val);
        else if (!strcmp(key,"current_version"))cfg->current_version= atoi(val);
        else if (!strcmp(key,"client_name"))    strncpy(cfg->client_name, val, sizeof cfg->client_name-1);
        else if (!strcmp(key,"save_dir"))       strncpy(cfg->save_dir,    val, sizeof cfg->save_dir-1);
        else if (!strcmp(key,"log_file"))       strncpy(cfg->log_file,    val, sizeof cfg->log_file-1);
    }
    fclose(f);
    return 0;
}

void print_server_config(const ServerConfig *cfg) {
    printf("┌──────────────────────────────────────┐\n");
    printf("│  Update Server Configuration          │\n");
    printf("├──────────────────────────────────────┤\n");
    printf("│  port            : %-6d             │\n", cfg->port);
    printf("│  latest version  : %-6d             │\n", cfg->latest_version);
    printf("│  update file     : %-20s │\n", cfg->update_file);
    printf("│  max clients     : %-6d             │\n", cfg->max_clients);
    printf("│  log file        : %-20s │\n", cfg->log_file);
    printf("└──────────────────────────────────────┘\n\n");
}

void print_client_config(const ClientConfig *cfg) {
    printf("┌──────────────────────────────────────┐\n");
    printf("│  Client Configuration                 │\n");
    printf("├──────────────────────────────────────┤\n");
    printf("│  server          : %s:%-5d          │\n", cfg->server_ip, cfg->server_port);
    printf("│  current version : %-6d             │\n", cfg->current_version);
    printf("│  client name     : %-20s │\n", cfg->client_name);
    printf("│  save directory  : %-20s │\n", cfg->save_dir);
    printf("└──────────────────────────────────────┘\n\n");
}
