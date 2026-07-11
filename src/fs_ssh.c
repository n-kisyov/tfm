#include "fs.h"
#include "utils.h"
#include "ssh_config.h"
#include <stdio.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* SSH master connection session pool                                  */
/* ------------------------------------------------------------------ */
#define SSH_MAX_SESSIONS  4
#define SSH_IDLE_TIMEOUT  300000   /* 5 min in ms */
#define SSH_SOCKET_PREFIX L"tfm-ssh-"

typedef struct {
    wchar_t  conn_name[64];
    wchar_t  host[256];
    int      port;
    wchar_t  user[64];
    wchar_t  key_file[SSH_CFG_PATH_MAX];
    wchar_t  password[128];
    int      use_key;
    int      connected;
    DWORD    last_used;
    wchar_t  socket_path[MAX_PATH];
    HANDLE   master_proc;
} SshSession;

static SshSession g_sessions[SSH_MAX_SESSIONS];
static int       g_session_count = 0;
static CRITICAL_SECTION g_ssh_cs;
static int g_ssh_cs_init = 0;

static void ssh_lock(void) {
    if (!g_ssh_cs_init) { InitializeCriticalSection(&g_ssh_cs); g_ssh_cs_init = 1; }
    EnterCriticalSection(&g_ssh_cs);
}
static void ssh_unlock(void) { LeaveCriticalSection(&g_ssh_cs); }

static const wchar_t *ssh_exe_path(void) {
    static wchar_t path[MAX_PATH];
    if (!path[0]) {
        wchar_t windir[MAX_PATH];
        GetEnvironmentVariableW(L"SystemRoot", windir, MAX_PATH);
        swprintf_s(path, MAX_PATH, L"%s\\System32\\OpenSSH\\ssh.exe", windir);
    }
    return path;
}

static const wchar_t *sftp_exe_path(void) {
    static wchar_t path[MAX_PATH];
    if (!path[0]) {
        wchar_t windir[MAX_PATH];
        GetEnvironmentVariableW(L"SystemRoot", windir, MAX_PATH);
        swprintf_s(path, MAX_PATH, L"%s\\System32\\OpenSSH\\sftp.exe", windir);
    }
    return path;
}

static SshSession *ssh_find_session(const wchar_t *conn_name) {
    for (int i = 0; i < g_session_count; i++) {
        if (wcscmp(g_sessions[i].conn_name, conn_name) == 0)
            return &g_sessions[i];
    }
    return NULL;
}

static SshSession *ssh_ensure_connected(const wchar_t *conn_name) {
    ssh_lock();

    /* check if session exists and is connected */
    SshSession *s = ssh_find_session(conn_name);
    if (s && s->connected) {
        s->last_used = GetTickCount();
        ssh_unlock();
        return s;
    }

    /* look up config */
    const wchar_t *cfg_path = ssh_config_path();
    SshConfig cfg;
    ssh_config_init(&cfg);
    ssh_config_load(&cfg, cfg_path);

    const SshConnection *conn = NULL;
    for (int i = 0; i < cfg.count; i++) {
        if (wcscmp(cfg.conns[i].name, conn_name) == 0) {
            conn = &cfg.conns[i];
            break;
        }
    }
    if (!conn) { ssh_unlock(); return NULL; }

    /* create or reuse session slot */
    if (!s) {
        if (g_session_count >= SSH_MAX_SESSIONS) {
            /* evict oldest */
            int oldest = 0;
            for (int i = 1; i < g_session_count; i++)
                if (g_sessions[i].last_used < g_sessions[oldest].last_used)
                    oldest = i;
            s = &g_sessions[oldest];
            if (s->master_proc) {
                TerminateProcess(s->master_proc, 0);
                CloseHandle(s->master_proc);
                s->master_proc = NULL;
            }
            DeleteFileW(s->socket_path);
            memset(s, 0, sizeof(SshSession));
        } else {
            s = &g_sessions[g_session_count++];
            memset(s, 0, sizeof(SshSession));
        }
    }

    wcscpy_s(s->conn_name, 64, conn->name);
    wcscpy_s(s->host, 256, conn->host);
    s->port = conn->port;
    wcscpy_s(s->user, 64, conn->user);
    wcscpy_s(s->key_file, SSH_CFG_PATH_MAX, conn->key_file);
    wcscpy_s(s->password, 128, conn->password);
    s->use_key = (wcscmp(conn->auth_method, L"key") == 0);

    /* socket path in %TEMP% */
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    swprintf_s(s->socket_path, MAX_PATH, L"%s\\%s%s", tmp, SSH_SOCKET_PREFIX, conn->name);

    DeleteFileW(s->socket_path);

    /* build ssh command: ssh -M -S <socket> -f -N -o StrictHostKeyChecking=accept-new ... */
    wchar_t ssh_cmd[2048];
    swprintf_s(ssh_cmd, 2048,
               L"\"%s\" -M -S \"%s\" -f -N "
               L"-o StrictHostKeyChecking=accept-new "
               L"-o ServerAliveInterval=60 "
               L"-p %d %s@%s",
               ssh_exe_path(), s->socket_path,
               s->port, s->user, s->host);

    if (s->use_key && s->key_file[0])
        swprintf_s(ssh_cmd + wcslen(ssh_cmd), 2048 - wcslen(ssh_cmd),
                   L" -i \"%s\"", s->key_file);

    /* launch ssh master */
    STARTUPINFOW si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {0};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    if (!CreateProcessW(NULL, ssh_cmd, NULL, NULL, FALSE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        ssh_unlock();
        return NULL;
    }
    CloseHandle(pi.hThread);
    s->master_proc = pi.hProcess;

    /* wait briefly for the connection to establish */
    WaitForSingleObject(pi.hProcess, 5000);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);

    /* if ssh exited quickly with non-zero, connection failed */
    if (exit_code != STILL_ACTIVE && exit_code != 0) {
        CloseHandle(pi.hProcess);
        s->master_proc = NULL;
        s->connected = 0;
        ssh_unlock();
        return NULL;
    }

    s->connected = 1;
    s->last_used = GetTickCount();
    ssh_unlock();
    return s;
}

/* run sftp in batch mode, capture output */
static int sftp_run(SshSession *s, const wchar_t *commands, wchar_t **output, int *out_len) {
    if (!s || !s->connected) return -1;

    wchar_t cmd[4096];
    swprintf_s(cmd, 4096,
               L"\"%s\" -o ControlPath=\"%s\" -o StrictHostKeyChecking=accept-new -b - %s@%s",
               sftp_exe_path(), s->socket_path, s->user, s->host);

    HANDLE h_stdin_r, h_stdin_w, h_stdout_r, h_stdout_w;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };

    CreatePipe(&h_stdin_r, &h_stdin_w, &sa, 0);
    CreatePipe(&h_stdout_r, &h_stdout_w, &sa, 0);
    SetHandleInformation(h_stdin_w, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    SetHandleInformation(h_stdout_r, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

    STARTUPINFOW si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {0};
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = h_stdin_r;
    si.hStdOutput = h_stdout_w;
    si.hStdError = h_stdout_w;

    if (!CreateProcessW(NULL, cmd, NULL, NULL, TRUE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(h_stdin_r); CloseHandle(h_stdin_w);
        CloseHandle(h_stdout_r); CloseHandle(h_stdout_w);
        return -1;
    }
    CloseHandle(h_stdin_r);
    CloseHandle(h_stdout_w);

    /* feed commands */
    char *mb_cmds = NULL;
    int mb_len = 0;
    if (commands) {
        mb_len = WideCharToMultiByte(CP_UTF8, 0, commands, -1, NULL, 0, NULL, NULL);
        mb_cmds = (char *)malloc(mb_len + 2);
        WideCharToMultiByte(CP_UTF8, 0, commands, -1, mb_cmds, mb_len, NULL, NULL);
        mb_cmds[mb_len] = '\n';
        DWORD w;
        WriteFile(h_stdin_w, mb_cmds, mb_len, &w, NULL);
        WriteFile(h_stdin_w, "\n", 1, &w, NULL);
        free(mb_cmds);
    }
    CloseHandle(h_stdin_w);

    /* read output */
    char obuf[8192];
    DWORD total = 0;
    DWORD rd;
    while (ReadFile(h_stdout_r, obuf + total, sizeof(obuf) - total - 1, &rd, NULL) && rd > 0) {
        total += rd;
        if (total >= sizeof(obuf) - 1) break;
    }
    obuf[total] = 0;
    CloseHandle(h_stdout_r);

    WaitForSingleObject(pi.hProcess, 15000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (output && out_len) {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, obuf, -1, NULL, 0);
        *output = (wchar_t *)calloc(wlen + 1, sizeof(wchar_t));
        MultiByteToWideChar(CP_UTF8, 0, obuf, -1, *output, wlen);
        *out_len = wlen > 0 ? wlen - 1 : 0;
    }

    return 0;
}

/* parse sftp ls -l output: "drwxr-xr-x    2 user group    4096 Jul 11 10:30 dirname" */
static int parse_ls_line(const wchar_t *line, FileEntry *e) {
    if (!line || !line[0]) return 0;
    wchar_t perms[16], date[32], time[16], name[256];
    int fields = swscanf_s(line, L"%15s %*d %*s %*s %*d %31s %15s %255[^\r\n]",
                           perms, 16, date, 32, time, 16, name, 256);
    if (fields < 4) return 0;

    wcscpy_s(e->name, 256, name);
    e->type = (perms[0] == L'd') ? ENTRY_DIR :
              (perms[0] == L'l') ? ENTRY_SYMLINK : ENTRY_FILE;
    e->size = 0;
    e->attrs = (e->type == ENTRY_DIR) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    memset(&e->modified, 0, sizeof(e->modified));
    return 1;
}

/* ------------------------------------------------------------------ */
/* FsProvider implementation                                           */
/* ------------------------------------------------------------------ */

static int ssh_fs_list_dir(const wchar_t *path, FileEntry **entries, int *count) {
    *entries = NULL;
    *count = 0;

    /* path format: \\ssh\ConnName\remote\path */
    /* skip \\ssh\ */
    const wchar_t *rest = path + 6;
    if (wcsncmp(path, L"\\\\ssh\\", 6) != 0) return -1;

    /* extract conn name (up to next backslash or end) */
    wchar_t conn_name[64];
    const wchar_t *slash = wcschr(rest, L'\\');
    int name_len = slash ? (int)(slash - rest) : (int)wcslen(rest);
    wcsncpy_s(conn_name, 64, rest, name_len);
    conn_name[name_len] = 0;

    /* remote path: after the first backslash */
    const wchar_t *remote = slash ? slash + 1 : L".";
    /* convert backslashes to forward slashes */
    wchar_t unix_path[SSH_CFG_PATH_MAX];
    wcscpy_s(unix_path, SSH_CFG_PATH_MAX, remote);
    for (wchar_t *p = unix_path; *p; p++) if (*p == L'\\') *p = L'/';

    SshSession *s = ssh_ensure_connected(conn_name);
    if (!s) return -1;

    /* build sftp commands */
    wchar_t cmds[4096];
    if (unix_path[0])
        swprintf_s(cmds, 4096, L"cd \"%s\"\nls -l", unix_path);
    else
        wcscpy_s(cmds, 4096, L"ls -l");

    wchar_t *output = NULL;
    int out_len = 0;
    if (sftp_run(s, cmds, &output, &out_len) < 0) return -1;
    if (!output) return -1;

    /* count lines for pre-alloc */
    int line_count = 0;
    for (int i = 0; i < out_len; i++)
        if (output[i] == L'\n') line_count++;

    int cap = line_count + 4;
    if (cap < 64) cap = 64;
    FileEntry *list = (FileEntry *)calloc(cap, sizeof(FileEntry));
    int cnt = 0;

    const wchar_t *line_start = output;
    for (int i = 0; i <= out_len; i++) {
        if (output[i] == L'\n' || output[i] == 0) {
            int llen = (int)(&output[i] - line_start);
            if (llen > 0) {
                wchar_t line[512];
                wcsncpy_s(line, 512, line_start, llen > 511 ? 511 : llen);
                line[llen > 511 ? 511 : llen] = 0;
                if (cnt >= cap) {
                    cap *= 2;
                    list = (FileEntry *)realloc(list, cap * sizeof(FileEntry));
                }
                if (parse_ls_line(line, &list[cnt])) cnt++;
            }
            line_start = &output[i + 1];
        }
    }

    free(output);
    *entries = list;
    *count = cnt;
    return 0;
}

static int ssh_fs_stat(const wchar_t *path, FileEntry *out) {
    memset(out, 0, sizeof(FileEntry));

    const wchar_t *rest = path + 6;
    if (wcsncmp(path, L"\\\\ssh\\", 6) != 0) return -1;

    wchar_t conn_name[64];
    const wchar_t *slash = wcschr(rest, L'\\');
    int name_len = slash ? (int)(slash - rest) : (int)wcslen(rest);
    wcsncpy_s(conn_name, 64, rest, name_len);
    conn_name[name_len] = 0;

    SshSession *s = ssh_ensure_connected(conn_name);
    if (!s) return -1;

    const wchar_t *remote = slash ? slash + 1 : L".";
    wchar_t unix_path[SSH_CFG_PATH_MAX];
    wcscpy_s(unix_path, SSH_CFG_PATH_MAX, remote);
    for (wchar_t *p = unix_path; *p; p++) if (*p == L'\\') *p = L'/';

    wchar_t cmds[4096];
    swprintf_s(cmds, 4096, L"ls -ld \"%s\"", unix_path);

    wchar_t *output = NULL;
    int out_len = 0;
    sftp_run(s, cmds, &output, &out_len);
    if (output) {
        parse_ls_line(output, out);
        const wchar_t *name = wcsrchr(remote, L'/');
        if (!name) name = remote; else name++;
        wcscpy_s(out->name, 256, name);
        free(output);
    }
    return out->name[0] ? 0 : -1;
}

static int ssh_fs_exists(const wchar_t *path) {
    if (wcsncmp(path, L"\\\\ssh\\", 6) != 0) return 0;
    FileEntry e;
    return ssh_fs_stat(path, &e) == 0 ? 1 : 0;
}

static int ssh_fs_is_dir(const wchar_t *path) {
    FileEntry e;
    if (ssh_fs_stat(path, &e) != 0) return 0;
    return (e.type == ENTRY_DIR) ? 1 : 0;
}

static void ssh_fs_free_entries(FileEntry *entries) {
    free(entries);
}

static int ssh_fs_mkdir(const wchar_t *path) {
    const wchar_t *rest = path + 6;
    if (wcsncmp(path, L"\\\\ssh\\", 6) != 0) return -1;

    wchar_t conn_name[64];
    const wchar_t *slash = wcschr(rest, L'\\');
    int name_len = slash ? (int)(slash - rest) : (int)wcslen(rest);
    wcsncpy_s(conn_name, 64, rest, name_len);
    conn_name[name_len] = 0;

    SshSession *s = ssh_ensure_connected(conn_name);
    if (!s) return -1;

    const wchar_t *remote = slash ? slash + 1 : L".";
    wchar_t unix_path[SSH_CFG_PATH_MAX];
    wcscpy_s(unix_path, SSH_CFG_PATH_MAX, remote);
    for (wchar_t *p = unix_path; *p; p++) if (*p == L'\\') *p = L'/';

    wchar_t cmds[1024];
    swprintf_s(cmds, 1024, L"mkdir \"%s\"", unix_path);

    wchar_t *output = NULL;
    int out_len = 0;
    int ret = sftp_run(s, cmds, &output, &out_len);
    if (output) free(output);
    return ret;
}

FsProvider fs_ssh = {
    ssh_fs_list_dir,
    ssh_fs_stat,
    ssh_fs_exists,
    ssh_fs_is_dir,
    ssh_fs_free_entries,
    ssh_fs_mkdir
};
