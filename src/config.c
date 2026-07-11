#include "config.h"
#include "json.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>

void config_set_defaults(Config *c) {
    memset(c, 0, sizeof(Config));
    wcscpy_s(c->theme_path, CONFIG_MAX_PATH, L"themes/default.json");
    c->startup_tab_counts[0] = 1;
    c->startup_tab_counts[1] = 1;
    wchar_t *home = get_home_dir();
    if (!home || wcslen(home) == 0) home = L"C:\\";
    for (int i = 0; i < 2; i++) {
        wcscpy_s(c->startup_dirs[i][0], CONFIG_MAX_PATH, home);
    }
    c->show_hidden = 0;
    c->sort_by = 0;
    c->sort_reverse = 0;
    c->confirm_delete = 1;
    c->panel_split_pct = 50;
}

const wchar_t *config_get_path(void) {
    static wchar_t path[MAX_PATH_LEN] = {0};
    if (!path[0]) {
        wchar_t *dir = get_config_dir();
        CreateDirectoryW(dir, NULL);
        swprintf_s(path, MAX_PATH_LEN, L"%s\\config.json", dir);
    }
    return path;
}

int config_load(Config *c, const wchar_t *path) {
    config_set_defaults(c);
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

    const wchar_t *tp = json_get_str(&root, L"theme", NULL);
    if (tp) wcscpy_s(c->theme_path, CONFIG_MAX_PATH, tp);
    c->show_hidden = json_get_bool(&root, L"show_hidden", 0);
    c->sort_by = (int)json_get_num(&root, L"sort_by", 0);
    c->sort_reverse = json_get_bool(&root, L"sort_reverse", 0);
    c->confirm_delete = json_get_bool(&root, L"confirm_delete", 1);
    c->panel_split_pct = (int)json_get_num(&root, L"panel_split_pct", 50);
    if (c->panel_split_pct < 20) c->panel_split_pct = 20;
    if (c->panel_split_pct > 80) c->panel_split_pct = 80;

    JsonValue *panels = json_get(&root, L"panels");
    if (panels && panels->type == JSON_ARRAY) {
        for (int pi = 0; pi < 2 && pi < panels->arr.count; pi++) {
            JsonValue *pv = json_arr_get(panels, pi);
            if (!pv || pv->type != JSON_OBJECT) continue;
            JsonValue *tabs = json_get(pv, L"tabs");
            if (tabs && tabs->type == JSON_ARRAY && tabs->arr.count > 0) {
                JsonValue *tv = json_arr_get(tabs, 0);
                if (tv && tv->type == JSON_STRING && tv->str_val[0])
                    wcscpy_s(c->startup_dirs[pi][0], CONFIG_MAX_PATH, tv->str_val);
            }
            c->startup_tab_counts[pi] = 1;
            JsonValue *drives = json_get(pv, L"drives");
            if (drives && drives->type == JSON_ARRAY) {
                for (int d = 0; d < drives->arr.count && d < 26; d++) {
                    JsonValue *dv = json_arr_get(drives, d);
                    if (dv && dv->type == JSON_STRING && dv->str_val[0])
                        wcscpy_s(c->drive_paths[pi][d], CONFIG_MAX_PATH, dv->str_val);
                }
            }
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

int config_save(const Config *c, const wchar_t *path) {
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    wchar_t *buf = (wchar_t *)calloc(65536, sizeof(wchar_t));
    if (!buf) { CloseHandle(h); return 0; }
    int cap = 65536;
    int pos = 0;

    pos = buf_puts(buf, cap, pos, L"{\r\n");

    pos = buf_puts(buf, cap, pos, L"  \"theme\": ");
    pos = buf_json_string(buf, cap, pos, c->theme_path);
    pos = buf_puts(buf, cap, pos, L",\r\n");

    pos = buf_puts(buf, cap, pos, L"  \"sort_by\": ");
    wchar_t num[32];
    swprintf_s(num, 32, L"%d", c->sort_by);
    pos = buf_puts(buf, cap, pos, num);
    pos = buf_puts(buf, cap, pos, L",\r\n");

    pos = buf_puts(buf, cap, pos, L"  \"sort_reverse\": ");
    pos = buf_puts(buf, cap, pos, c->sort_reverse ? L"true" : L"false");
    pos = buf_puts(buf, cap, pos, L",\r\n");

    pos = buf_puts(buf, cap, pos, L"  \"panel_split_pct\": ");
    swprintf_s(num, 32, L"%d", c->panel_split_pct);
    pos = buf_puts(buf, cap, pos, num);
    pos = buf_puts(buf, cap, pos, L",\r\n");

    pos = buf_puts(buf, cap, pos, L"  \"panels\": [\r\n");
    for (int pi = 0; pi < 2; pi++) {
        pos = buf_puts(buf, cap, pos, L"    {\r\n");
        pos = buf_puts(buf, cap, pos, L"      \"tabs\": [\r\n");
        for (int ti = 0; ti < c->startup_tab_counts[pi]; ti++) {
            pos = buf_puts(buf, cap, pos, L"        ");
            pos = buf_json_string(buf, cap, pos, c->startup_dirs[pi][ti]);
            pos = buf_puts(buf, cap, pos, ti < c->startup_tab_counts[pi] - 1 ? L",\r\n" : L"\r\n");
        }
        pos = buf_puts(buf, cap, pos, L"      ],\r\n");
        pos = buf_puts(buf, cap, pos, L"      \"drives\": [\r\n");
        for (int d = 0; d < 26; d++) {
            pos = buf_puts(buf, cap, pos, L"        ");
            pos = buf_json_string(buf, cap, pos,
                c->drive_paths[pi][d][0] ? c->drive_paths[pi][d] : L"");
            pos = buf_puts(buf, cap, pos, d < 25 ? L",\r\n" : L"\r\n");
        }
        pos = buf_puts(buf, cap, pos, L"      ]\r\n");
        pos = buf_puts(buf, cap, pos, L"    }");
        pos = buf_puts(buf, cap, pos, pi < 1 ? L",\r\n" : L"\r\n");
    }
    pos = buf_puts(buf, cap, pos, L"  ]\r\n");
    pos = buf_puts(buf, cap, pos, L"}\r\n");

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
