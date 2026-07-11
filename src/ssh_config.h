#ifndef SSH_CONFIG_H
#define SSH_CONFIG_H

#include <wchar.h>

#define SSH_MAX_CONNECTIONS 16
#define SSH_CFG_PATH_MAX    520

typedef struct {
    wchar_t name[64];
    wchar_t host[256];
    int     port;
    wchar_t user[64];
    wchar_t password[128];
    wchar_t remote_path[SSH_CFG_PATH_MAX];
} SshConnection;

typedef struct {
    SshConnection conns[SSH_MAX_CONNECTIONS];
    int           count;
} SshConfig;

void ssh_config_init(SshConfig *cfg);
int  ssh_config_load(SshConfig *cfg, const wchar_t *path);
int  ssh_config_save(const SshConfig *cfg, const wchar_t *path);
const wchar_t *ssh_config_path(void);

#endif
