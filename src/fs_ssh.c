#include <winsock2.h>
#include <ws2tcpip.h>

#include "fs.h"
#include "utils.h"
#include "ssh_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <libssh2.h>
#include <libssh2_sftp.h>

/* ------------------------------------------------------------------ */
/* connection pool                                                      */
/* ------------------------------------------------------------------ */
#define SSH_POOL_MAX 4

typedef struct {
    wchar_t            name[64];
    LIBSSH2_SESSION   *session;
    LIBSSH2_SFTP      *sftp;
    SOCKET             sock;
    DWORD              last_used;
    int                active;
} SshSess;

static SshSess g_pool[SSH_POOL_MAX];

static SshSess g_pool[SSH_POOL_MAX];
static int    g_inited = 0;

static void ssh_init(void) {
    if (!g_inited) {
        libssh2_init(0);
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        g_inited = 1;
    }
}

static SshSess *ssh_pool_find(const wchar_t *name) {
    for (int i = 0; i < SSH_POOL_MAX; i++)
        if (g_pool[i].active && wcscmp(g_pool[i].name, name) == 0)
            return &g_pool[i];
    return NULL;
}

static void ssh_pool_close(SshSess *s) {
    if (s->sftp)  { libssh2_sftp_shutdown(s->sftp); s->sftp = NULL; }
    if (s->session) {
        libssh2_session_disconnect(s->session, "bye");
        libssh2_session_free(s->session);
        s->session = NULL;
    }
    if (s->sock != INVALID_SOCKET) { closesocket(s->sock); s->sock = INVALID_SOCKET; }
    s->active = 0;
    s->name[0] = 0;
}

static SshSess *ssh_pool_alloc(void) {
    for (int i = 0; i < SSH_POOL_MAX; i++)
        if (!g_pool[i].active) { memset(&g_pool[i], 0, sizeof(SshSess)); return &g_pool[i]; }
    int oldest = 0;
    for (int i = 1; i < SSH_POOL_MAX; i++)
        if (g_pool[i].last_used < g_pool[oldest].last_used) oldest = i;
    ssh_pool_close(&g_pool[oldest]);
    memset(&g_pool[oldest], 0, sizeof(SshSess));
    return &g_pool[oldest];
}

static int lookup_conn(const wchar_t *conn_name, SshConnection *out) {
    SshConfig cfg;
    ssh_config_init(&cfg);
    const wchar_t *cfg_path = ssh_config_path();
    if (!ssh_config_load(&cfg, cfg_path)) return -1;
    for (int i = 0; i < cfg.count; i++) {
        if (wcscmp(cfg.conns[i].name, conn_name) == 0) {
            *out = cfg.conns[i];
            return 0;
        }
    }
    return -1;
}

static int ssh_resolve(const SshConnection *conn, struct sockaddr_storage *addr, int *addr_len) {
    memset(addr, 0, sizeof(*addr));
    *addr_len = 0;
    wchar_t port_s[16];
    swprintf_s(port_s, 16, L"%d", conn->port);

    ADDRINFOW hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (GetAddrInfoW(conn->host, port_s, &hints, &result) != 0) return -1;
    *addr_len = (int)result->ai_addrlen;
    memcpy(addr, result->ai_addr, result->ai_addrlen);
    FreeAddrInfoW(result);
    return 0;
}

static SshSess *ssh_connect(const wchar_t *conn_name) {
    ssh_init();

    SshSess *s = ssh_pool_find(conn_name);
    if (s && s->session && s->sftp) {
        s->last_used = GetTickCount();
        return s;
    }

    SshConnection cfg;
    if (lookup_conn(conn_name, &cfg) < 0) {
        MessageBoxW(NULL, conn_name, L"SSH: config lookup failed", MB_ICONERROR);
        return NULL;
    }

    struct sockaddr_storage addr;
    int addr_len = 0;
    if (ssh_resolve(&cfg, &addr, &addr_len) < 0) {
        MessageBoxW(NULL, cfg.host, L"SSH: DNS resolution failed", MB_ICONERROR);
        return NULL;
    }

    SOCKET sock = socket(addr.ss_family, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        MessageBoxW(NULL, L"socket() failed", L"SSH Error", MB_ICONERROR);
        return NULL;
    }

    if (connect(sock, (struct sockaddr *)&addr, addr_len) != 0) {
        wchar_t msg[256];
        swprintf_s(msg, 256, L"TCP connect failed.\nHost: %s", cfg.host);
        MessageBoxW(NULL, msg, L"SSH Error", MB_ICONERROR);
        closesocket(sock);
        return NULL;
    }

    LIBSSH2_SESSION *session = libssh2_session_init();
    if (!session) { closesocket(sock); return NULL; }
    libssh2_session_set_timeout(session, 15000);

    if (libssh2_session_handshake(session, sock) != 0) {
        char *errmsg = NULL;
        int errlen = 0;
        int rc = libssh2_session_last_error(session, &errmsg, &errlen, 0);
        wchar_t msg[1024];
        swprintf_s(msg, 1024, L"SSH handshake failed (rc=%d):\n%S\n\n"
                   L"The server likely requires Ed25519 host keys.\n"
                   L"Fix: on the server run:\n"
                   L"ssh-keygen -t rsa -f /etc/ssh/ssh_host_rsa_key\n"
                   L"systemctl restart sshd",
                   rc, errmsg ? errmsg : "unknown");
        MessageBoxW(NULL, msg, L"SSH Error", MB_ICONERROR);
        libssh2_session_free(session);
        closesocket(sock);
        return NULL;
    }

    /* check the fingerprint */
    const char *fingerprint = libssh2_hostkey_hash(session, LIBSSH2_HOSTKEY_HASH_SHA256);
    (void)fingerprint; /* accept all host keys for now */

    char user[128], pass[128];
    WideCharToMultiByte(CP_UTF8, 0, cfg.user, -1, user, 128, NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, cfg.password, -1, pass, 128, NULL, NULL);

    int rc = libssh2_userauth_password(session, user, pass);
    SecureZeroMemory(pass, sizeof(pass));
    if (rc != 0) {
        libssh2_session_disconnect(session, "auth fail");
        libssh2_session_free(session);
        closesocket(sock);
        return NULL;
    }

    LIBSSH2_SFTP *sf = libssh2_sftp_init(session);
    if (!sf) {
        libssh2_session_disconnect(session, "sftp fail");
        libssh2_session_free(session);
        closesocket(sock);
        return NULL;
    }

    s = ssh_pool_alloc();
    wcscpy_s(s->name, 64, conn_name);
    s->session = session;
    s->sftp    = sf;
    s->sock    = sock;
    s->active  = 1;
    s->last_used = GetTickCount();
    return s;
}

/* ------------------------------------------------------------------ */
/* path helpers                                                         */
/* ------------------------------------------------------------------ */
static wchar_t *ssh_conn_name(const wchar_t *path, wchar_t *buf, int sz) {
    const wchar_t *rest = path + 6;
    const wchar_t *slash = wcschr(rest, L'\\');
    int len = slash ? (int)(slash - rest) : (int)wcslen(rest);
    wcsncpy_s(buf, sz, rest, len);
    buf[len] = 0;
    return buf;
}

static wchar_t *ssh_remote_path(const wchar_t *path, wchar_t *buf, int sz) {
    const wchar_t *rest = path + 6;
    const wchar_t *slash = wcschr(rest, L'\\');
    const wchar_t *rpath = slash ? slash + 1 : L"";
    wcscpy_s(buf, sz, rpath[0] ? rpath : L".");
    for (wchar_t *p = buf; *p; p++) if (*p == L'\\') *p = L'/';
    if (buf[0] == 0) wcscpy_s(buf, sz, L".");
    return buf;
}

/* ------------------------------------------------------------------ */
/* FsProvider implementation                                            */
/* ------------------------------------------------------------------ */

static int ssh_fs_list_dir(const wchar_t *path, FileEntry **entries, int *count) {
    *entries = NULL;
    *count = 0;
    if (wcsncmp(path, L"\\\\ssh\\", 6) != 0) return -1;

    wchar_t conn[64], rpath[SSH_CFG_PATH_MAX];
    ssh_conn_name(path, conn, 64);
    ssh_remote_path(path, rpath, SSH_CFG_PATH_MAX);

    SshSess *s = ssh_connect(conn);
    if (!s) return -1;

    char upath[SSH_CFG_PATH_MAX];
    WideCharToMultiByte(CP_UTF8, 0, rpath, -1, upath, SSH_CFG_PATH_MAX, NULL, NULL);

    LIBSSH2_SFTP_HANDLE *dh = libssh2_sftp_opendir(s->sftp, upath);
    if (!dh) return -1;

    int cap = 64;
    FileEntry *list = (FileEntry *)calloc(cap, sizeof(FileEntry));
    int cnt = 0;
    char name_buf[512];
    LIBSSH2_SFTP_ATTRIBUTES attrs;

    while (libssh2_sftp_readdir(dh, name_buf, sizeof(name_buf), &attrs) > 0) {
        if (name_buf[0] == '.' && (name_buf[1] == 0 || (name_buf[1] == '.' && name_buf[2] == 0)))
            continue;
        if (cnt >= cap) {
            cap *= 2;
            list = (FileEntry *)realloc(list, cap * sizeof(FileEntry));
        }
        MultiByteToWideChar(CP_UTF8, 0, name_buf, -1, list[cnt].name, 256);
        list[cnt].type = LIBSSH2_SFTP_S_ISDIR(attrs.permissions) ? ENTRY_DIR :
                         LIBSSH2_SFTP_S_ISLNK(attrs.permissions) ? ENTRY_SYMLINK : ENTRY_FILE;
        list[cnt].size = attrs.filesize;
        list[cnt].attrs = list[cnt].type == ENTRY_DIR ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        list[cnt].modified.dwLowDateTime  = (DWORD)(attrs.mtime & 0xFFFFFFFF);
        list[cnt].modified.dwHighDateTime = 0;
        cnt++;
    }
    libssh2_sftp_closedir(dh);

    *entries = list;
    *count = cnt;
    return 0;
}

static int ssh_fs_stat(const wchar_t *path, FileEntry *out) {
    memset(out, 0, sizeof(FileEntry));
    if (wcsncmp(path, L"\\\\ssh\\", 6) != 0) return -1;

    wchar_t conn[64], rpath[SSH_CFG_PATH_MAX];
    ssh_conn_name(path, conn, 64);
    ssh_remote_path(path, rpath, SSH_CFG_PATH_MAX);

    SshSess *s = ssh_connect(conn);
    if (!s) return -1;

    char upath[SSH_CFG_PATH_MAX];
    WideCharToMultiByte(CP_UTF8, 0, rpath, -1, upath, SSH_CFG_PATH_MAX, NULL, NULL);

    LIBSSH2_SFTP_ATTRIBUTES attrs;
    if (libssh2_sftp_stat(s->sftp, upath, &attrs) != 0) return -1;

    const wchar_t *nm = wcsrchr(rpath, L'/');
    if (!nm) nm = rpath; else nm++;
    wcscpy_s(out->name, 256, nm);
    out->type = LIBSSH2_SFTP_S_ISDIR(attrs.permissions) ? ENTRY_DIR :
                LIBSSH2_SFTP_S_ISLNK(attrs.permissions) ? ENTRY_SYMLINK : ENTRY_FILE;
    out->size = attrs.filesize;
    out->attrs = out->type == ENTRY_DIR ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    return 0;
}

static int ssh_fs_exists(const wchar_t *path) {
    if (wcsncmp(path, L"\\\\ssh\\", 6) != 0) return 0;
    FileEntry e;
    return ssh_fs_stat(path, &e) == 0;
}

static int ssh_fs_is_dir(const wchar_t *path) {
    FileEntry e;
    if (ssh_fs_stat(path, &e) != 0) return 0;
    return e.type == ENTRY_DIR;
}

static void ssh_fs_free_entries(FileEntry *entries) { free(entries); }

static int ssh_fs_mkdir(const wchar_t *path) {
    if (wcsncmp(path, L"\\\\ssh\\", 6) != 0) return -1;
    wchar_t conn[64], rpath[SSH_CFG_PATH_MAX];
    ssh_conn_name(path, conn, 64);
    ssh_remote_path(path, rpath, SSH_CFG_PATH_MAX);

    SshSess *s = ssh_connect(conn);
    if (!s) return -1;

    char upath[SSH_CFG_PATH_MAX];
    WideCharToMultiByte(CP_UTF8, 0, rpath, -1, upath, SSH_CFG_PATH_MAX, NULL, NULL);
    return libssh2_sftp_mkdir(s->sftp, upath, 0755);
}

/* ------------------------------------------------------------------ */
/* SSH file transfer helpers (used by bgop threads)                     */
/* ------------------------------------------------------------------ */

int ssh_is_ssh_path(const wchar_t *path) {
    return wcsncmp(path, L"\\\\ssh\\", 6) == 0;
}

static int sftp_read_file(SshSess *s, const char *rpath, HANDLE local_file) {
    LIBSSH2_SFTP_HANDLE *fh = libssh2_sftp_open(s->sftp, rpath, LIBSSH2_FXF_READ, 0);
    if (!fh) return -1;
    char buf[65536];
    ssize_t n;
    int ok = 0;
    while ((n = libssh2_sftp_read(fh, buf, sizeof(buf))) > 0) {
        DWORD w;
        WriteFile(local_file, buf, (DWORD)n, &w, NULL);
        ok = 1;
    }
    libssh2_sftp_close(fh);
    return ok ? 0 : -1;
}

static int sftp_write_file(SshSess *s, const char *rpath, HANDLE local_file) {
    DWORD size = GetFileSize(local_file, NULL);
    SetFilePointer(local_file, 0, NULL, FILE_BEGIN);
    LIBSSH2_SFTP_HANDLE *fh = libssh2_sftp_open(s->sftp, rpath,
        LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
        LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR |
        LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH);
    if (!fh) return -1;
    char buf[65536];
    DWORD total = 0, rd;
    while (total < size && ReadFile(local_file, buf, sizeof(buf), &rd, NULL) && rd > 0) {
        ssize_t wr = libssh2_sftp_write(fh, buf, rd);
        if (wr < 0) { libssh2_sftp_close(fh); return -1; }
        total += rd;
    }
    libssh2_sftp_close(fh);
    return 0;
}

int ssh_transfer_file(const wchar_t *src, const wchar_t *dest);

int ssh_delete_path(const wchar_t *path) {
    if (!ssh_is_ssh_path(path)) return -1;
    wchar_t conn[64], rpath[SSH_CFG_PATH_MAX];
    ssh_conn_name(path, conn, 64);
    ssh_remote_path(path, rpath, SSH_CFG_PATH_MAX);

    SshSess *s = ssh_connect(conn);
    if (!s) return -1;

    char up[SSH_CFG_PATH_MAX];
    WideCharToMultiByte(CP_UTF8, 0, rpath, -1, up, SSH_CFG_PATH_MAX, NULL, NULL);

    /* try unlink first (file), if that fails try rmdir (empty dir), then recursive */
    if (libssh2_sftp_unlink(s->sftp, up) == 0) return 0;
    if (libssh2_sftp_rmdir(s->sftp, up) == 0) return 0;

    /* recursive delete via SFTP unix paths */
    LIBSSH2_SFTP_HANDLE *dh = libssh2_sftp_opendir(s->sftp, up);
    if (!dh) return -1;

    char name_buf[512];
    LIBSSH2_SFTP_ATTRIBUTES attrs;
    while (libssh2_sftp_readdir(dh, name_buf, sizeof(name_buf), &attrs) > 0) {
        if (name_buf[0] == '.' && (name_buf[1] == 0 || (name_buf[1] == '.' && name_buf[2] == 0)))
            continue;
        char sub[SSH_CFG_PATH_MAX * 2];
        snprintf(sub, sizeof(sub), "%s/%s", up, name_buf);
        if (LIBSSH2_SFTP_S_ISDIR(attrs.permissions)) {
            /* recursively delete subdirectory */
            /* build a fake \\ssh path for recursion */
            wchar_t w_full[SSH_CFG_PATH_MAX + 64];
            swprintf_s(w_full, SSH_CFG_PATH_MAX + 64, L"\\\\ssh\\%s\\%s/%s", conn, rpath, name_buf);
            ssh_delete_path(w_full);
        } else {
            libssh2_sftp_unlink(s->sftp, sub);
        }
    }
    libssh2_sftp_closedir(dh);
    return libssh2_sftp_rmdir(s->sftp, up);
}

int ssh_transfer_file(const wchar_t *src, const wchar_t *dest) {
    int src_ssh = ssh_is_ssh_path(src);
    int dst_ssh = ssh_is_ssh_path(dest);

    if (src_ssh && dst_ssh) return -1;  /* blocked by ops.c */
    if (!src_ssh && !dst_ssh) return -1; /* should use CopyFileW instead */

    if (src_ssh) {
        /* download */
        wchar_t conn[64], rpath[SSH_CFG_PATH_MAX];
        ssh_conn_name(src, conn, 64);
        ssh_remote_path(src, rpath, SSH_CFG_PATH_MAX);
        SshSess *s = ssh_connect(conn);
        if (!s) return -1;
        char up[SSH_CFG_PATH_MAX];
        WideCharToMultiByte(CP_UTF8, 0, rpath, -1, up, SSH_CFG_PATH_MAX, NULL, NULL);
        HANDLE hf = CreateFileW(dest, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hf == INVALID_HANDLE_VALUE) return -1;
        int r = sftp_read_file(s, up, hf);
        CloseHandle(hf);
        if (r < 0) DeleteFileW(dest);
        return r;
    } else {
        /* upload */
        wchar_t conn[64], rpath[SSH_CFG_PATH_MAX];
        ssh_conn_name(dest, conn, 64);
        ssh_remote_path(dest, rpath, SSH_CFG_PATH_MAX);
        SshSess *s = ssh_connect(conn);
        if (!s) return -1;
        char up[SSH_CFG_PATH_MAX];
        WideCharToMultiByte(CP_UTF8, 0, rpath, -1, up, SSH_CFG_PATH_MAX, NULL, NULL);
        HANDLE hf = CreateFileW(src, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hf == INVALID_HANDLE_VALUE) return -1;
        int r = sftp_write_file(s, up, hf);
        CloseHandle(hf);
        return r;
    }
}

FsProvider fs_ssh = {
    ssh_fs_list_dir,
    ssh_fs_stat,
    ssh_fs_exists,
    ssh_fs_is_dir,
    ssh_fs_free_entries,
    ssh_fs_mkdir
};

void ssh_cleanup(void) {
    for (int i = 0; i < SSH_POOL_MAX; i++) {
        if (g_pool[i].active) {
            ssh_pool_close(&g_pool[i]);
        }
    }
}
