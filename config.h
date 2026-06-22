#ifndef CONFIG_H
#define CONFIG_H

/* Server config */
typedef struct {
    int  port;
    int  latest_version;
    char update_file[256];
    int  max_clients;
    char log_file[256];
    char bind_ip[64];
} ServerConfig;

/* Client config */
typedef struct {
    char server_ip[64];
    int  server_port;
    int  current_version;
    char client_name[64];
    char save_dir[256];
    char log_file[256];
} ClientConfig;

int load_server_config(const char *path, ServerConfig *cfg);
int load_client_config(const char *path, ClientConfig *cfg);
void print_server_config(const ServerConfig *cfg);
void print_client_config(const ClientConfig *cfg);

#endif
