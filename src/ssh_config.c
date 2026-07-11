#include "ssh_config.h"
#include "json.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>

void ssh_config_init(SshConfig *cfg) {
    memset(cfg, 0, sizeof(SshConfig));
}

const wchar_t *ssh_config_path(void) {
    static wchar_t path[MAX_PATH_LEN] = {0};
    if (!path[0]) {
        wchar_t *dir = get_config_dir();
        CreateDirectoryW(dir, NULL);
        swprintf_s(path, MAX_PATH_LEN, L"%s\\ssh.json", dir);
    }
    return path;
}

int ssh_config_load(SshConfig *cfg, const wchar_t *path) {
    ssh_config_init(cfg);
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD size = GetFileSize(h, NULL);
    if (size == INVALID_FILE_SIZE || size > 65536) { CloseHandle(h); return 0; }
    wchar_t *buf = (wchar_t *)calloc(size + 2, sizeof(wchar_t));
    if (!buf) { CloseHandle(h); return 0; }
    char *mb_buf = (char *)malloc(size + 1);
    if (!mb_buf) { free(buf); CloseHandle(h); return 0; }
    DWORD read = 0;
    ReadFile(h, mb_buf, size, &read, NULL);
    CloseHandle(h);
    mb_buf[read] = 0;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, mb_buf, -1, buf, (int)(size + 1));
    free(mb_buf);
    if (wlen <= 0) { free(buf); return 0; }
    JsonValue root = json_parse(buf);
    free(buf);
    if (root.type != JSON_OBJECT) return 0;

    JsonValue *conns = json_get(&root, L"connections");
    if (conns && conns->type == JSON_ARRAY) {
        for (int i = 0; i < conns->arr.count && i < SSH_MAX_CONNECTIONS; i++) {
            JsonValue *cv = json_arr_get(conns, i);
            if (!cv || cv->type != JSON_OBJECT) continue;
            SshConnection *c = &cfg->conns[cfg->count];
            const wchar_t *s;
            s = json_get_str(cv, L"name", NULL);     if (s) wcsncpy_s(c->name, 64, s, 63);
            s = json_get_str(cv, L"host", NULL);     if (s) wcsncpy_s(c->host, 256, s, 255);
            s = json_get_str(cv, L"user", NULL);     if (s) wcsncpy_s(c->user, 64, s, 63);
            s = json_get_str(cv, L"password", NULL); if (s) wcsncpy_s(c->password, 128, s, 127);
            s = json_get_str(cv, L"remote_path", NULL); if (s) wcscpy_s(c->remote_path, SSH_CFG_PATH_MAX, s);
            c->port = (int)json_get_num(cv, L"port", 22);
            if (c->port <= 0) c->port = 22;
            if (c->name[0]) cfg->count++;
        }
    }
    json_free(&root);
    return 1;
}

static int buf_puts(wchar_t *buf, int cap, int pos, const wchar_t *str) {
    while (*str && pos < cap - 1) buf[pos++] = *str++;
    buf[pos] = 0;
    return pos;
}

static int buf_json_string(wchar_t *buf, int cap, int pos, const wchar_t *str) {
    if (pos >= cap - 2) return pos;
    buf[pos++] = L'"';
    if (str) {
        for (const wchar_t *s = str; *s && pos < cap - 3; s++) {
            if (pos >= cap - 2) break;
            if (*s == L'\\')      { buf[pos++] = L'\\'; buf[pos++] = L'\\'; }
            else if (*s == L'"')  { buf[pos++] = L'\\'; buf[pos++] = L'"'; }
            else if (*s == L'\r') { buf[pos++] = L'\\'; buf[pos++] = L'r'; }
            else if (*s == L'\n') { buf[pos++] = L'\\'; buf[pos++] = L'n'; }
            else if (*s == L'\t') { buf[pos++] = L'\\'; buf[pos++] = L't'; }
            else                   buf[pos++] = *s;
        }
    }
    if (pos < cap - 1) buf[pos++] = L'"';
    buf[pos] = 0;
    return pos;
}

#define BUF_JSON_KV(buf, cap, pos, key, val) do { \
    pos = buf_puts(buf, cap, pos, L"      \"" key "\": "); \
    pos = buf_json_string(buf, cap, pos, val); \
    pos = buf_puts(buf, cap, pos, L",\r\n"); \
} while(0)

int ssh_config_save(const SshConfig *cfg, const wchar_t *path) {
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    wchar_t *buf = (wchar_t *)calloc(32768, sizeof(wchar_t));
    if (!buf) { CloseHandle(h); return 0; }
    int cap = 32768, pos = 0;

    pos = buf_puts(buf, cap, pos, L"{\r\n  \"connections\": [\r\n");
    for (int i = 0; i < cfg->count; i++) {
        const SshConnection *c = &cfg->conns[i];
        pos = buf_puts(buf, cap, pos, L"    {\r\n");
        wchar_t num[16];
        BUF_JSON_KV(buf, cap, pos, "name", c->name);
        BUF_JSON_KV(buf, cap, pos, "host", c->host);
        swprintf_s(num, 16, L"%d", c->port);
        pos = buf_puts(buf, cap, pos, L"      \"port\": ");
        pos = buf_puts(buf, cap, pos, num);
        pos = buf_puts(buf, cap, pos, L",\r\n");
        BUF_JSON_KV(buf, cap, pos, "user", c->user);
        BUF_JSON_KV(buf, cap, pos, "password", c->password);
        BUF_JSON_KV(buf, cap, pos, "remote_path", c->remote_path);
        pos = buf_puts(buf, cap, pos, L"      \"_end\": \"\"\r\n");
        pos = buf_puts(buf, cap, pos, L"    }");
        pos = buf_puts(buf, cap, pos, i < cfg->count - 1 ? L",\r\n" : L"\r\n");
    }
    pos = buf_puts(buf, cap, pos, L"  ]\r\n}\r\n");

    char *mb_buf = (char *)calloc(pos * 4 + 1, 1);
    int mb_len = 0;
    if (mb_buf) {
        mb_len = WideCharToMultiByte(CP_UTF8, 0, buf, pos, mb_buf, (int)(pos * 4), NULL, NULL);
    }
    free(buf);
    DWORD written;
    BOOL ok = WriteFile(h, mb_buf, mb_len, &written, NULL);
    free(mb_buf);
    CloseHandle(h);
    return ok && (written == (DWORD)mb_len);
}
