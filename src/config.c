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
    wchar_t *buf = (wchar_t *)calloc(size / 2 + 2, sizeof(wchar_t));
    if (!buf) { CloseHandle(h); return 0; }
    char *mb_buf = (char *)malloc(size + 1);
    if (!mb_buf) { free(buf); CloseHandle(h); return 0; }
    DWORD read = 0;
    ReadFile(h, mb_buf, size, &read, NULL);
    CloseHandle(h);
    mb_buf[read] = 0;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, mb_buf, -1, buf, (int)(size / 2 + 2));
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
            if (tabs && tabs->type == JSON_ARRAY) {
                int tc = 0;
                for (int ti = 0; ti < tabs->arr.count && ti < MAX_TABS; ti++) {
                    JsonValue *tv = json_arr_get(tabs, ti);
                    if (tv && tv->type == JSON_STRING) {
                        wcscpy_s(c->startup_dirs[pi][ti], CONFIG_MAX_PATH, tv->str_val);
                        tc++;
                    }
                }
                c->startup_tab_counts[pi] = tc > 0 ? tc : 1;
            }
        }
    }
    json_free(&root);
    return 1;
}

int config_save(const Config *c, const wchar_t *path) {
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    wchar_t buf[8192];
    int pos = 0;
    pos += swprintf_s(buf + pos, 8192 - pos, L"{\r\n");
    pos += swprintf_s(buf + pos, 8192 - pos, L"  \"theme\": \"%s\",\r\n", c->theme_path);
    pos += swprintf_s(buf + pos, 8192 - pos, L"  \"show_hidden\": %s,\r\n", c->show_hidden ? L"true" : L"false");
    pos += swprintf_s(buf + pos, 8192 - pos, L"  \"sort_by\": %d,\r\n", c->sort_by);
    pos += swprintf_s(buf + pos, 8192 - pos, L"  \"sort_reverse\": %s,\r\n", c->sort_reverse ? L"true" : L"false");
    pos += swprintf_s(buf + pos, 8192 - pos, L"  \"confirm_delete\": %s,\r\n", c->confirm_delete ? L"true" : L"false");
    pos += swprintf_s(buf + pos, 8192 - pos, L"  \"panel_split_pct\": %d,\r\n", c->panel_split_pct);
    pos += swprintf_s(buf + pos, 8192 - pos, L"  \"panels\": [\r\n");
    for (int pi = 0; pi < 2; pi++) {
        pos += swprintf_s(buf + pos, 8192 - pos, L"    {\r\n");
        pos += swprintf_s(buf + pos, 8192 - pos, L"      \"tabs\": [\r\n");
        for (int ti = 0; ti < c->startup_tab_counts[pi]; ti++) {
            pos += swprintf_s(buf + pos, 8192 - pos, L"        \"%s\"%s\r\n",
                              c->startup_dirs[pi][ti],
                              ti < c->startup_tab_counts[pi] - 1 ? L"," : L"");
        }
        pos += swprintf_s(buf + pos, 8192 - pos, L"      ]\r\n");
        pos += swprintf_s(buf + pos, 8192 - pos, L"    }%s\r\n", pi < 1 ? L"," : L"");
    }
    pos += swprintf_s(buf + pos, 8192 - pos, L"  ]\r\n");
    pos += swprintf_s(buf + pos, 8192 - pos, L"}\r\n");
    char mb_buf[16384];
    int mb_len = WideCharToMultiByte(CP_UTF8, 0, buf, pos, mb_buf, (int)sizeof(mb_buf), NULL, NULL);
    DWORD written;
    BOOL ok = WriteFile(h, mb_buf, mb_len, &written, NULL);
    CloseHandle(h);
    return ok && (written == (DWORD)mb_len);
}
