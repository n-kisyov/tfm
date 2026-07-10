#include "theme.h"
#include "json.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void theme_set_default(Theme *t) {
    memset(t, 0, sizeof(Theme));
    strcpy_s(t->name, sizeof(t->name), "Default Dark");
    t->colors[COLOR_BG]              = 0x1E1E2E;
    t->colors[COLOR_PANEL_BORDER]    = 0x45475A;
    t->colors[COLOR_FOCUS_BORDER]    = 0x89B4FA;
    t->colors[COLOR_TAB_ACTIVE]      = 0x89B4FA;
    t->colors[COLOR_TAB_INACTIVE]    = 0x585B70;
    t->colors[COLOR_TAB_ACTIVE_BG]   = 0x313244;
    t->colors[COLOR_DIR]             = 0x89B4FA;
    t->colors[COLOR_FILE]            = 0xCDD6F4;
    t->colors[COLOR_SYMLINK]         = 0xA6E3A1;
    t->colors[COLOR_SELECTED_BG]     = 0x313244;
    t->colors[COLOR_SELECTED_FG]     = 0xCDD6F4;
    t->colors[COLOR_TAGGED]          = 0xF9E2AF;
    t->colors[COLOR_TAGGED_BG]       = 0x45475A;
    t->colors[COLOR_STATUS_BAR]      = 0x45475A;
    t->colors[COLOR_STATUS_BG]       = 0x45475A;
    t->colors[COLOR_STATUS_FG]       = 0xCDD6F4;
    t->colors[COLOR_CMDLINE]         = 0xCDD6F4;
    t->colors[COLOR_DIALOG_BORDER]   = 0xFAB387;
    t->colors[COLOR_DIALOG_BG]       = 0x313244;
    t->colors[COLOR_ERROR]           = 0xF38BA8;
    t->colors[COLOR_PROGRESS]        = 0xA6E3A1;
}

static const wchar_t *color_role_names[] = {
    L"bg",
    L"panel_border",
    L"focus_border",
    L"tab_active",
    L"tab_inactive",
    L"tab_active_bg",
    L"dir",
    L"file",
    L"symlink",
    L"selected_bg",
    L"selected_fg",
    L"tagged",
    L"tagged_bg",
    L"status_bar",
    L"status_bg",
    L"status_fg",
    L"cmdline",
    L"dialog_border",
    L"dialog_bg",
    L"error",
    L"progress",
};

static uint32_t parse_hex_color(const wchar_t *str) {
    if (!str || wcslen(str) < 7 || str[0] != L'#') return 0xFFFFFF;
    return (uint32_t)wcstol(str + 1, NULL, 16) & 0xFFFFFF;
}

int theme_load(Theme *t, const wchar_t *path) {
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
    BOOL ok = ReadFile(h, mb_buf, size, &read, NULL);
    CloseHandle(h);
    if (!ok) { free(buf); free(mb_buf); return 0; }
    mb_buf[read] = 0;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, mb_buf, -1, buf, (int)(size / 2 + 2));
    free(mb_buf);
    if (wlen <= 0) { free(buf); return 0; }

    JsonValue root = json_parse(buf);
    free(buf);
    if (root.type != JSON_OBJECT) return 0;

    JsonValue *name_v = json_get(&root, L"name");
    if (name_v && name_v->type == JSON_STRING) {
        WideCharToMultiByte(CP_UTF8, 0, name_v->str_val, -1, t->name, (int)sizeof(t->name), NULL, NULL);
    }

    JsonValue *colors = json_get(&root, L"colors");
    if (colors && colors->type == JSON_OBJECT) {
        for (int i = 0; i < COLOR_COUNT; i++) {
            JsonValue *cv = json_get(colors, color_role_names[i]);
            if (cv && cv->type == JSON_STRING) {
                t->colors[i] = parse_hex_color(cv->str_val);
            }
        }
    }
    json_free(&root);
    return 1;
}

uint32_t theme_get(const Theme *t, ColorRole role) {
    return t->colors[role];
}

void theme_ansi_fg(char *buf, size_t sz, uint32_t rgb) {
    snprintf(buf, sz, "\x1b[38;2;%d;%d;%dm", (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

void theme_ansi_bg(char *buf, size_t sz, uint32_t rgb) {
    snprintf(buf, sz, "\x1b[48;2;%d;%d;%dm", (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

void theme_ansi_reset(char *buf, size_t sz) {
    snprintf(buf, sz, "\x1b[0m");
}
