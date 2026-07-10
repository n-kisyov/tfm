#include "ui.h"
#include "input.h"
#include <stdio.h>
#include <stdlib.h>

static HANDLE g_hStdout = NULL;
static DWORD g_oldOutMode = 0;
static int   g_termW = 80, g_termH = 24;
static int   g_frame_begun = 0;

void ui_init(void) {
    g_hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleOutputCP(CP_UTF8);

    GetConsoleMode(g_hStdout, &g_oldOutMode);
    DWORD mode = g_oldOutMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT;
    SetConsoleMode(g_hStdout, mode);

    /* disable newline auto-return for new console */
    mode |= DISABLE_NEWLINE_AUTO_RETURN;
    SetConsoleMode(g_hStdout, mode);

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(g_hStdout, &csbi)) {
        g_termW = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        g_termH = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }

    /* switch to alternate screen buffer for clean start */
    DWORD written;
    const char *alt_buf = "\x1b[?1049h\x1b[2J\x1b[H";
    WriteFile(g_hStdout, alt_buf, (DWORD)strlen(alt_buf), &written, NULL);
}

void ui_shutdown(void) {
    DWORD written;
    const char *restore = "\x1b[?1049l\x1b[0m";
    WriteFile(g_hStdout, restore, (DWORD)strlen(restore), &written, NULL);

    if (g_hStdout) {
        SetConsoleMode(g_hStdout, g_oldOutMode);
    }
}

void ui_begin_frame(void) {
    g_frame_begun = 1;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(g_hStdout, &csbi)) {
        g_termW = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        g_termH = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }
}

void ui_end_frame(void) {
    g_frame_begun = 0;
}

void ui_get_term_size(int *w, int *h) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(g_hStdout, &csbi)) {
        g_termW = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        g_termH = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }
    if (w) *w = g_termW;
    if (h) *h = g_termH;
}

static void write_ansi(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (len > 0) {
        DWORD written;
        WriteFile(g_hStdout, buf, (DWORD)len, &written, NULL);
    }
}

void ui_set_fg(uint32_t rgb) {
    write_ansi("\x1b[38;2;%d;%d;%dm", (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

void ui_set_bg(uint32_t rgb) {
    write_ansi("\x1b[48;2;%d;%d;%dm", (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

void ui_reset_colors(void) {
    write_ansi("\x1b[0m");
}

void ui_set_bold(void) {
    write_ansi("\x1b[1m");
}

void ui_set_dim(void) {
    write_ansi("\x1b[2m");
}

void ui_reverse(void) {
    write_ansi("\x1b[7m");
}

void ui_move(int x, int y) {
    write_ansi("\x1b[%d;%dH", y + 1, x + 1);
}

void ui_clear_line(int y) {
    ui_move(0, y);
    write_ansi("\x1b[2K");
}

void ui_clear_rect(int x, int y, int w, int h) {
    for (int i = 0; i < h; i++) {
        ui_move(x, y + i);
        for (int j = 0; j < w; j++) {
            write_ansi(" ");
        }
    }
}

void ui_draw_text(int x, int y, const wchar_t *text) {
    if (!text) return;
    ui_move(x, y);
    DWORD written;
    WriteConsoleW(g_hStdout, text, (DWORD)wcslen(text), &written, NULL);
}

void ui_draw_text_trunc(int x, int y, int max_w, const wchar_t *text) {
    if (!text || max_w <= 0) return;
    ui_move(x, y);
    int len = (int)wcslen(text);
    if (len > max_w) {
        wchar_t buf[512];
        wcsncpy_s(buf, 512, text, max_w);
        buf[max_w] = 0;
        DWORD written;
        WriteConsoleW(g_hStdout, buf, max_w, &written, NULL);
    } else {
        DWORD written;
        WriteConsoleW(g_hStdout, text, len, &written, NULL);
    }
}

void ui_draw_text_centered(int y, int max_w, const wchar_t *text) {
    if (!text) return;
    int len = (int)wcslen(text);
    int x = (max_w - len) / 2;
    if (x < 0) x = 0;
    ui_draw_text(x, y, text);
}

void ui_draw_char(int x, int y, wchar_t ch) {
    ui_move(x, y);
    DWORD written;
    WriteConsoleW(g_hStdout, &ch, 1, &written, NULL);
}

void ui_draw_h_line(int x, int y, int w, wchar_t ch) {
    ui_move(x, y);
    for (int i = 0; i < w; i++) {
        DWORD written;
        WriteConsoleW(g_hStdout, &ch, 1, &written, NULL);
    }
}

void ui_draw_v_line(int x, int y, int h, wchar_t ch) {
    for (int i = 0; i < h; i++) {
        ui_draw_char(x, y + i, ch);
    }
}

void ui_draw_rect(int x, int y, int w, int h) {
    if (w < 2 || h < 2) return;
    /* corners */
    ui_draw_char(x, y, L'┌');
    ui_draw_char(x + w - 1, y, L'┐');
    ui_draw_char(x, y + h - 1, L'└');
    ui_draw_char(x + w - 1, y + h - 1, L'┘');
    /* top and bottom */
    ui_draw_h_line(x + 1, y, w - 2, L'─');
    ui_draw_h_line(x + 1, y + h - 1, w - 2, L'─');
    /* sides */
    ui_draw_v_line(x, y + 1, h - 2, L'│');
    ui_draw_v_line(x + w - 1, y + 1, h - 2, L'│');
}

void ui_draw_rect_content(int x, int y, int w, int h) {
    /* same as draw_rect but fills interior with spaces */
    if (w < 2 || h < 2) return;
    ui_draw_char(x, y, L'┌');
    ui_draw_char(x + w - 1, y, L'┐');
    ui_draw_char(x, y + h - 1, L'└');
    ui_draw_char(x + w - 1, y + h - 1, L'┘');
    ui_draw_h_line(x + 1, y, w - 2, L'─');
    ui_draw_h_line(x + 1, y + h - 1, w - 2, L'─');
    ui_draw_v_line(x, y + 1, h - 2, L'│');
    ui_draw_v_line(x + w - 1, y + 1, h - 2, L'│');
    /* fill interior */
    for (int i = 1; i < h - 1; i++) {
        ui_move(x + 1, y + i);
        for (int j = 0; j < w - 2; j++) {
            DWORD w2;
            WriteConsoleW(g_hStdout, L" ", 1, &w2, NULL);
        }
    }
}

void ui_fill_rect(int x, int y, int w, int h, wchar_t ch) {
    for (int i = 0; i < h; i++) {
        ui_draw_h_line(x, y + i, w, ch);
    }
}

void ui_hide_cursor(void) {
    write_ansi("\x1b[?25l");
}

void ui_show_cursor(int x, int y) {
    ui_move(x, y);
    write_ansi("\x1b[?25h");
}

void ui_message_box(const Theme *theme, const wchar_t *title, const wchar_t *msg) {
    int tw, th;
    ui_get_term_size(&tw, &th);
    int bw = (int)wcslen(msg) + 8;
    if (bw < (int)wcslen(title) + 8) bw = (int)wcslen(title) + 8;
    if (bw > tw - 4) bw = tw - 4;
    if (bw < 20) bw = 20;
    int bh = 5;
    int bx = (tw - bw) / 2;
    int by = (th - bh) / 2;

    ui_hide_cursor();
    ui_set_bg(theme_get(theme, COLOR_DIALOG_BG));
    ui_set_fg(theme_get(theme, COLOR_FILE));
    ui_draw_rect_content(bx, by, bw, bh);
    ui_set_fg(theme_get(theme, COLOR_DIALOG_BORDER));
    ui_draw_rect(bx, by, bw, bh);
    ui_set_fg(theme_get(theme, COLOR_SELECTED_FG));
    ui_draw_text(bx + 2, by + 1, title);
    ui_set_fg(theme_get(theme, COLOR_FILE));
    ui_draw_text(bx + 2, by + 2, msg);
    ui_set_fg(theme_get(theme, COLOR_DIALOG_BORDER));
    ui_draw_text_centered(by + bh - 2, bw, L"Press any key to continue...");
    ui_reset_colors();

    /* wait for key */
    KeyEvent ev;
    while (input_poll(&ev)) {
        if (ev.code && ev.code != KEY_RESIZE) break;
    }
}

int ui_confirm_dialog(const Theme *theme, const wchar_t *title, const wchar_t *msg) {
    int tw, th;
    ui_get_term_size(&tw, &th);
    int bw = (int)wcslen(msg) + 8;
    if (bw < (int)wcslen(title) + 8) bw = (int)wcslen(title) + 8;
    if (bw > tw - 4) bw = tw - 4;
    if (bw < 30) bw = 30;
    int bh = 6;
    int bx = (tw - bw) / 2;
    int by = (th - bh) / 2;

    int selected = 1; /* 1 = yes, 0 = no */

    while (1) {
        ui_hide_cursor();
        ui_set_bg(theme_get(theme, COLOR_DIALOG_BG));
        ui_set_fg(theme_get(theme, COLOR_FILE));
        ui_draw_rect_content(bx, by, bw, bh);
        ui_set_fg(theme_get(theme, COLOR_DIALOG_BORDER));
        ui_draw_rect(bx, by, bw, bh);

        ui_set_fg(theme_get(theme, COLOR_SELECTED_FG));
        ui_set_bold();
        ui_draw_text(bx + 2, by + 1, title);
        ui_reset_colors();

        ui_set_bg(theme_get(theme, COLOR_DIALOG_BG));
        ui_set_fg(theme_get(theme, COLOR_FILE));
        ui_draw_text(bx + 2, by + 2, msg);

        int yb = by + bh - 2;
        if (selected) {
            ui_set_bg(theme_get(theme, COLOR_SELECTED_BG));
            ui_set_fg(theme_get(theme, COLOR_SELECTED_FG));
            ui_set_bold();
            ui_draw_text(bx + 4, yb, L"  Yes  ");
            ui_reset_colors();
            ui_set_bg(theme_get(theme, COLOR_DIALOG_BG));
            ui_set_fg(theme_get(theme, COLOR_FILE));
            ui_draw_text(bx + bw - 12, yb, L"  No   ");
        } else {
            ui_set_bg(theme_get(theme, COLOR_DIALOG_BG));
            ui_set_fg(theme_get(theme, COLOR_FILE));
            ui_draw_text(bx + 4, yb, L"  Yes  ");
            ui_set_bg(theme_get(theme, COLOR_SELECTED_BG));
            ui_set_fg(theme_get(theme, COLOR_SELECTED_FG));
            ui_set_bold();
            ui_draw_text(bx + bw - 12, yb, L"  No   ");
            ui_reset_colors();
        }
        ui_reset_colors();

        KeyEvent ev;
        while (input_poll(&ev)) {
            if (ev.code == KEY_RESIZE) continue;
            if (ev.code == KEY_LEFT || ev.code == KEY_RIGHT || ev.code == KEY_TAB) {
                selected = !selected;
                break;
            }
            if (ev.code == KEY_ENTER) {
                return selected;
            }
            if (ev.code == KEY_ESC) {
                return 0;
            }
            if (ev.code == KEY_CHAR) {
                if (ev.ch == L'y' || ev.ch == L'Y') return 1;
                if (ev.ch == L'n' || ev.ch == L'N') return 0;
            }
            break;
        }
    }
}

int ui_input_dialog(const Theme *theme, const wchar_t *title, wchar_t *buf, int buf_len) {
    int tw, th;
    ui_get_term_size(&tw, &th);
    int bw = 50;
    if (bw > tw - 4) bw = tw - 4;
    int bh = 5;
    int bx = (tw - bw) / 2;
    int by = (th - bh) / 2;

    buf[0] = 0;
    int cursor = 0;
    int len = 0;

    while (1) {
        ui_hide_cursor();
        ui_set_bg(theme_get(theme, COLOR_DIALOG_BG));
        ui_draw_rect_content(bx, by, bw, bh);
        ui_set_fg(theme_get(theme, COLOR_DIALOG_BORDER));
        ui_draw_rect(bx, by, bw, bh);

        ui_set_bg(theme_get(theme, COLOR_DIALOG_BG));
        ui_set_fg(theme_get(theme, COLOR_SELECTED_FG));
        ui_set_bold();
        ui_draw_text(bx + 2, by + 1, title);
        ui_reset_colors();

        /* input area */
        int in_w = bw - 6;
        ui_set_bg(theme_get(theme, COLOR_BG));
        ui_set_fg(theme_get(theme, COLOR_CMDLINE));
        ui_draw_h_line(bx + 2, by + 2, in_w + 2, L' ');
        ui_move(bx + 3, by + 2);
        DWORD w2;
        WriteConsoleW(g_hStdout, buf, len, &w2, NULL);

        /* cursor highlight */
        if (cursor < len) {
            ui_move(bx + 3 + cursor, by + 2);
            ui_set_bg(theme_get(theme, COLOR_SELECTED_BG));
            ui_set_fg(theme_get(theme, COLOR_SELECTED_FG));
            WriteConsoleW(g_hStdout, &buf[cursor], 1, &w2, NULL);
        }
        ui_reset_colors();

        ui_set_bg(theme_get(theme, COLOR_DIALOG_BG));
        ui_set_fg(theme_get(theme, COLOR_DIALOG_BORDER));
        ui_draw_text_centered(by + bh - 2, bw, L"Enter=confirm  Esc=cancel");
        ui_reset_colors();

        KeyEvent ev;
        while (input_poll(&ev)) {
            if (ev.code == KEY_RESIZE) continue;
            if (ev.code == KEY_ESC) { buf[0] = 0; return 0; }
            if (ev.code == KEY_ENTER) return 1;
            if (ev.code == KEY_LEFT && cursor > 0) { cursor--; break; }
            if (ev.code == KEY_RIGHT && cursor < len) { cursor++; break; }
            if (ev.code == KEY_HOME) { cursor = 0; break; }
            if (ev.code == KEY_END) { cursor = len; break; }
            if (ev.code == KEY_BACKSPACE && cursor > 0) {
                memmove(buf + cursor - 1, buf + cursor, (len - cursor + 1) * sizeof(wchar_t));
                cursor--; len--;
                break;
            }
            if (ev.code == KEY_DELETE && cursor < len) {
                memmove(buf + cursor, buf + cursor + 1, (len - cursor) * sizeof(wchar_t));
                len--;
                break;
            }
            if (ev.code == KEY_CHAR && ev.ch >= 32 && len < buf_len - 1) {
                memmove(buf + cursor + 1, buf + cursor, (len - cursor + 1) * sizeof(wchar_t));
                buf[cursor] = ev.ch;
                cursor++; len++;
                break;
            }
            break;
        }
    }
}
