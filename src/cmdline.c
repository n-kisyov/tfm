#include "cmdline.h"
#include "ui.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>

void cmdline_init(CmdLine *c) {
    memset(c, 0, sizeof(CmdLine));
}

void cmdline_free(CmdLine *c) {
    for (int i = 0; i < c->history_count; i++) free(c->history[i]);
    free(c->last_output);
    memset(c, 0, sizeof(CmdLine));
}

void cmdline_insert(CmdLine *c, wchar_t ch) {
    if (c->length >= CMDLINE_MAX_LEN - 1) return;
    memmove(c->buffer + c->cursor + 1, c->buffer + c->cursor, (c->length - c->cursor + 1) * sizeof(wchar_t));
    c->buffer[c->cursor] = ch;
    c->cursor++;
    c->length++;
}

void cmdline_backspace(CmdLine *c) {
    if (c->cursor <= 0) return;
    memmove(c->buffer + c->cursor - 1, c->buffer + c->cursor, (c->length - c->cursor + 1) * sizeof(wchar_t));
    c->cursor--;
    c->length--;
}

void cmdline_delete(CmdLine *c) {
    if (c->cursor >= c->length) return;
    memmove(c->buffer + c->cursor, c->buffer + c->cursor + 1, (c->length - c->cursor) * sizeof(wchar_t));
    c->length--;
}

void cmdline_cursor_left(CmdLine *c) {
    if (c->cursor > 0) c->cursor--;
}

void cmdline_cursor_right(CmdLine *c) {
    if (c->cursor < c->length) c->cursor++;
}

void cmdline_cursor_home(CmdLine *c) {
    c->cursor = 0;
}

void cmdline_cursor_end(CmdLine *c) {
    c->cursor = c->length;
}

void cmdline_history_prev(CmdLine *c) {
    if (c->history_count == 0) return;
    if (c->history_pos == 0 && c->length > 0) {
        /* save current to temp */
    }
    if (c->history_pos < c->history_count) {
        c->history_pos++;
        wcscpy_s(c->buffer, CMDLINE_MAX_LEN, c->history[c->history_count - c->history_pos]);
        c->length = (int)wcslen(c->buffer);
        c->cursor = c->length;
    }
}

void cmdline_history_next(CmdLine *c) {
    if (c->history_pos <= 0) return;
    c->history_pos--;
    if (c->history_pos == 0) {
        c->buffer[0] = 0;
        c->length = 0;
        c->cursor = 0;
    } else {
        wcscpy_s(c->buffer, CMDLINE_MAX_LEN, c->history[c->history_count - c->history_pos]);
        c->length = (int)wcslen(c->buffer);
        c->cursor = c->length;
    }
}

static void add_history(CmdLine *c, const wchar_t *cmd) {
    if (!cmd || cmd[0] == 0) return;
    /* don't duplicate last entry */
    if (c->history_count > 0 && wcscmp(c->history[c->history_count - 1], cmd) == 0) return;
    if (c->history_count >= CMDLINE_HISTORY_MAX) {
        free(c->history[0]);
        memmove(c->history, c->history + 1, (CMDLINE_HISTORY_MAX - 1) * sizeof(wchar_t *));
        c->history_count--;
    }
    c->history[c->history_count++] = wcsdup_safe(cmd);
    c->history_pos = 0;
}

int cmdline_execute(CmdLine *c) {
    if (c->length == 0) return 0;
    c->buffer[c->length] = 0;
    add_history(c, c->buffer);

    /* need to dup buffer before we clear it */
    wchar_t cmd[CMDLINE_MAX_LEN];
    wcscpy_s(cmd, CMDLINE_MAX_LEN, c->buffer);

    /* redirect to temp file for output */
    wchar_t tmp_path[520];
    wchar_t tmp_dir[520];
    GetTempPathW(520, tmp_dir);
    GetTempFileNameW(tmp_dir, L"tfm", 0, tmp_path);

    wchar_t full_cmd[CMDLINE_MAX_LEN + 128];
    swprintf_s(full_cmd, CMDLINE_MAX_LEN + 128, L"cmd.exe /c \"%s\" > \"%s\" 2>&1", cmd, tmp_path);

    /* hide cursor and save screen */
    ui_hide_cursor();

    PROCESS_INFORMATION pi = {0};
    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    if (CreateProcessW(NULL, full_cmd, NULL, NULL, FALSE,
                       CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    /* read output */
    free(c->last_output);
    c->last_output = NULL;

    HANDLE hf = CreateFileW(tmp_path, GENERIC_READ, FILE_SHARE_READ, NULL,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf != INVALID_HANDLE_VALUE) {
        DWORD fsize = GetFileSize(hf, NULL);
        if (fsize > 0 && fsize < 65536) {
            char *mb = (char *)malloc(fsize + 1);
            DWORD read = 0;
            ReadFile(hf, mb, fsize, &read, NULL);
            mb[read] = 0;
            c->last_output = (wchar_t *)calloc(fsize + 2, sizeof(wchar_t));
            MultiByteToWideChar(CP_UTF8, 0, mb, -1, c->last_output, (int)fsize + 1);
            c->show_output = 1;
            c->output_scroll = 0;
            free(mb);
        }
        CloseHandle(hf);
    }
    DeleteFileW(tmp_path);

    /* clear command line */
    c->buffer[0] = 0;
    c->length = 0;
    c->cursor = 0;
    return 1;
}

void cmdline_clear(CmdLine *c) {
    c->buffer[0] = 0;
    c->length = 0;
    c->cursor = 0;
}

void cmdline_render(const CmdLine *c, const Theme *theme, int x, int y, int w, int focused) {
    /* if showing output, render that instead */
    if (c->show_output && c->last_output) {
        /* output mode - render over panel area */
        int tw = w;
        int th = 10;
        int tx = x;
        int ty = y - th - 1;
        if (ty < 1) ty = 1;

        ui_set_bg(theme_get(theme, COLOR_DIALOG_BG));
        ui_set_fg(theme_get(theme, COLOR_DIALOG_BORDER));
        ui_draw_rect(tx, ty, tw, th + 2);
        ui_set_bg(theme_get(theme, COLOR_DIALOG_BG));
        ui_draw_rect_content(tx + 1, ty + 1, tw - 2, th);

        ui_set_bg(theme_get(theme, COLOR_DIALOG_BG));
        ui_set_fg(theme_get(theme, COLOR_FILE));
        /* render output lines */
        int line = 0;
        const wchar_t *p = c->last_output;
        while (*p && line < th) {
            const wchar_t *nl = wcschr(p, L'\n');
            int plen = nl ? (int)(nl - p) : (int)wcslen(p);
            if (plen > tw - 4) plen = tw - 4;
            wchar_t lbuf[512];
            wcsncpy_s(lbuf, 512, p, plen);
            lbuf[plen] = 0;
            ui_draw_text_trunc(tx + 2, ty + 1 + line, tw - 4, lbuf);
            line++;
            if (nl) p = nl + 1; else break;
        }
        ui_set_fg(theme_get(theme, COLOR_DIALOG_BORDER));
        ui_draw_text_centered(ty + th + 1, tw, L"Press any key to dismiss");
        ui_reset_colors();
        return;
    }

    /* render the command line normally */
    ui_set_fg(focused ? theme_get(theme, COLOR_FOCUS_BORDER) : theme_get(theme, COLOR_PANEL_BORDER));
    ui_draw_char(x, y, focused ? L'▶' : L'$');

    ui_set_bg(theme_get(theme, COLOR_BG));
    ui_set_fg(theme_get(theme, COLOR_CMDLINE));

    int text_w = w - 3;
    wchar_t disp[CMDLINE_MAX_LEN + 4];
    disp[0] = L' ';
    wcscpy_s(disp + 1, CMDLINE_MAX_LEN + 3, c->buffer);
    ui_draw_text_trunc(x + 1, y, text_w, disp);

    /* cursor */
    if (focused) {
        if (c->cursor < c->length) {
            ui_set_bg(theme_get(theme, COLOR_SELECTED_BG));
            ui_set_fg(theme_get(theme, COLOR_SELECTED_FG));
            ui_draw_char(x + 2 + c->cursor, y, c->buffer[c->cursor]);
        } else {
            ui_reset_colors();
            ui_set_bg(theme_get(theme, COLOR_CMDLINE));
            ui_draw_char(x + 2 + c->cursor, y, L' ');
        }
    }
    ui_reset_colors();
}
