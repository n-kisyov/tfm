#include "ui.h"
#include "input.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

static HANDLE g_hStdout = NULL;
static DWORD  g_oldOutMode = 0;
static int    g_termW = 80, g_termH = 24;

static wchar_t *g_buf = NULL;
static int      g_buf_len = 0;
static int      g_buf_cap = 0;

static uint32_t g_cur_fg = 0xFFFFFFFF;
static uint32_t g_cur_bg = 0xFFFFFFFF;
static int      g_bold = 0;
static int      g_dim  = 0;
static int      g_reverse = 0;

static void buf_append(const wchar_t *s, int len) {
    if (len <= 0) return;
    if (g_buf_len + len > g_buf_cap) {
        g_buf_cap = (g_buf_len + len) * 2;
        g_buf = (wchar_t *)realloc(g_buf, g_buf_cap * sizeof(wchar_t));
        if (!g_buf) abort();
    }
    memcpy(g_buf + g_buf_len, s, len * sizeof(wchar_t));
    g_buf_len += len;
}

static void buf_append_str(const wchar_t *s) {
    if (s) buf_append(s, (int)wcslen(s));
}

static void buf_fmt(const wchar_t *fmt, ...) {
    wchar_t tmp[512];
    va_list ap;
    va_start(ap, fmt);
    int len = _vsnwprintf_s(tmp, 512, 511, fmt, ap);
    va_end(ap);
    if (len > 0) buf_append(tmp, len);
}

static void emit_sgr(void) {
    uint32_t fg = g_cur_fg, bg = g_cur_bg;
    if (fg == 0xFFFFFFFF && bg == 0xFFFFFFFF && !g_bold && !g_dim && !g_reverse)
        return;

    buf_append_str(L"\x1b[0");
    if (fg != 0xFFFFFFFF)
        buf_fmt(L";38;2;%d;%d;%d", (fg >> 16) & 0xFF, (fg >> 8) & 0xFF, fg & 0xFF);
    if (bg != 0xFFFFFFFF)
        buf_fmt(L";48;2;%d;%d;%d", (bg >> 16) & 0xFF, (bg >> 8) & 0xFF, bg & 0xFF);
    if (g_bold)    buf_append_str(L";1");
    if (g_dim)     buf_append_str(L";2");
    if (g_reverse) buf_append_str(L";7");
    buf_append_str(L"m");
}

void ui_init(void) {
    g_hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleOutputCP(CP_UTF8);

    GetConsoleMode(g_hStdout, &g_oldOutMode);
    DWORD mode = g_oldOutMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT;
    mode |= DISABLE_NEWLINE_AUTO_RETURN;
    SetConsoleMode(g_hStdout, mode);

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(g_hStdout, &csbi)) {
        g_termW = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        g_termH = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }

    DWORD written;
    const char *alt = "\x1b[?1049h\x1b[2J\x1b[H";
    WriteFile(g_hStdout, alt, (DWORD)strlen(alt), &written, NULL);
}

void ui_shutdown(void) {
    free(g_buf);
    g_buf = NULL;
    g_buf_cap = 0;
    g_buf_len = 0;

    DWORD written;
    const char *restore = "\x1b[?1049l\x1b[0m";
    WriteFile(g_hStdout, restore, (DWORD)strlen(restore), &written, NULL);

    if (g_hStdout) {
        SetConsoleMode(g_hStdout, g_oldOutMode);
    }
}

void ui_begin_frame(void) {
    g_buf_len = 0;
    g_cur_fg = 0xFFFFFFFF;
    g_cur_bg = 0xFFFFFFFF;
    g_bold = 0;
    g_dim  = 0;
    g_reverse = 0;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(g_hStdout, &csbi)) {
        g_termW = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        g_termH = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }

    buf_append_str(L"\x1b[H");   /* home cursor for the frame */
}

void ui_end_frame(void) {
    DWORD written;
    WriteConsoleW(g_hStdout, g_buf, (DWORD)g_buf_len, &written, NULL);
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

void ui_set_fg(uint32_t rgb)   { g_cur_fg = rgb; }
void ui_set_bg(uint32_t rgb)   { g_cur_bg = rgb; }
void ui_set_bold(void)         { g_bold = 1; }
void ui_set_dim(void)          { g_dim  = 1; }
void ui_reverse(void)          { g_reverse = 1; }

void ui_reset_colors(void) {
    g_cur_fg = 0xFFFFFFFF;
    g_cur_bg = 0xFFFFFFFF;
    g_bold = 0;
    g_dim  = 0;
    g_reverse = 0;
}

void ui_move(int x, int y) {
    buf_fmt(L"\x1b[%d;%dH", y + 1, x + 1);
}

void ui_clear_line(int y) {
    ui_move(0, y);
    buf_append_str(L"\x1b[2K");
}

void ui_clear_rect(int x, int y, int w, int h) {
    (void)w;
    for (int i = 0; i < h; i++) {
        ui_move(x, y + i);
        buf_append_str(L"\x1b[K");
    }
}

void ui_draw_text(int x, int y, const wchar_t *text) {
    if (!text) return;
    emit_sgr();
    ui_move(x, y);
    buf_append_str(text);
}

void ui_draw_text_trunc(int x, int y, int max_w, const wchar_t *text) {
    if (!text || max_w <= 0) return;
    emit_sgr();
    ui_move(x, y);
    int len = (int)wcslen(text);
    if (len > max_w) len = max_w;
    buf_append(text, len);
    /* pad with spaces to clear any previous content */
    int rem = max_w - len;
    while (rem > 0) {
        buf_append_str(L" ");
        rem--;
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
    emit_sgr();
    ui_move(x, y);
    buf_append(&ch, 1);
}

void ui_draw_h_line(int x, int y, int w, wchar_t ch) {
    emit_sgr();
    ui_move(x, y);
    for (int i = 0; i < w; i++) {
        buf_append(&ch, 1);
    }
}

void ui_draw_v_line(int x, int y, int h, wchar_t ch) {
    for (int i = 0; i < h; i++) {
        ui_draw_char(x, y + i, ch);
    }
}

void ui_draw_rect(int x, int y, int w, int h) {
    if (w < 2 || h < 2) return;
    ui_draw_char(x, y, L'\x250c');           /* corner top-left */
    ui_draw_char(x + w - 1, y, L'\x2510');   /* corner top-right */
    ui_draw_char(x, y + h - 1, L'\x2514');   /* corner bottom-left */
    ui_draw_char(x + w - 1, y + h - 1, L'\x2518'); /* corner bottom-right */
    ui_draw_h_line(x + 1, y, w - 2, L'\x2500');     /* top */
    ui_draw_h_line(x + 1, y + h - 1, w - 2, L'\x2500'); /* bottom */
    ui_draw_v_line(x, y + 1, h - 2, L'\x2502');       /* left */
    ui_draw_v_line(x + w - 1, y + 1, h - 2, L'\x2502'); /* right */
}

void ui_draw_rect_content(int x, int y, int w, int h) {
    if (w < 2 || h < 2) return;
    ui_draw_char(x, y, L'\x250c');
    ui_draw_char(x + w - 1, y, L'\x2510');
    ui_draw_char(x, y + h - 1, L'\x2514');
    ui_draw_char(x + w - 1, y + h - 1, L'\x2518');
    ui_draw_h_line(x + 1, y, w - 2, L'\x2500');
    ui_draw_h_line(x + 1, y + h - 1, w - 2, L'\x2500');
    ui_draw_v_line(x, y + 1, h - 2, L'\x2502');
    ui_draw_v_line(x + w - 1, y + 1, h - 2, L'\x2502');
    for (int i = 1; i < h - 1; i++) {
        ui_move(x + 1, y + i);
        for (int j = 0; j < w - 2; j++) {
            buf_append_str(L" ");
        }
    }
}

void ui_fill_rect(int x, int y, int w, int h, wchar_t ch) {
    for (int i = 0; i < h; i++) {
        ui_draw_h_line(x, y + i, w, ch);
    }
}

void ui_hide_cursor(void) {
    buf_append_str(L"\x1b[?25l");
}

void ui_show_cursor(int x, int y) {
    ui_move(x, y);
    buf_append_str(L"\x1b[?25h");
}

void ui_clear_screen(void) {
    emit_sgr();
    for (int y = 0; y < g_termH; y++) {
        ui_move(0, y);
        for (int x = 0; x < g_termW; x++)
            buf_append_str(L" ");
    }
}

void ui_draw_history(const Theme *theme, int tw, int th,
                     const void *bgtask_v,
                     const void *hist_v, int hist_count,
                     int show_current) {
    /* avoid including bgop.h — we only need a few known fields */
    typedef struct {
        volatile LONG active, finished, visible, error;
        int op_type, total_items;
        volatile LONG done_items;
        wchar_t current_file[260], title[64];
        DWORD start_ticks;
    } Bt;
    typedef struct { int op_type, total, done; wchar_t desc[128]; int status; } Hr;
    const Bt *bt = (const Bt *)bgtask_v;
    const Hr *hist = (const Hr *)hist_v;

    int lines = hist_count + (show_current ? 1 : 0) + 3; /* title + header + footer */
    int max_lines = th - 4;
    if (lines > max_lines) lines = max_lines;
    int bw = tw - 8;
    if (bw < 50) bw = tw - 2;
    if (bw < 30) bw = 30;
    int bh = lines + 2;
    int bx = (tw - bw) / 2;
    int by = (th - bh) / 2;
    if (by < 0) by = 0;

    /* background */
    ui_set_bg(theme_get(theme, COLOR_DIALOG_BG));
    ui_set_fg(theme_get(theme, COLOR_FILE));
    ui_fill_rect(bx, by, bw, bh, L' ');
    ui_reset_colors();
    ui_set_fg(theme_get(theme, COLOR_FOCUS_BORDER));
    ui_draw_rect(bx, by, bw, bh);

    /* title */
    ui_set_bg(theme_get(theme, COLOR_DIALOG_BG));
    ui_set_fg(theme_get(theme, COLOR_SELECTED_FG));
    ui_set_bold();
    ui_draw_text(bx + 2, by + 1, L"  Operation history");
    if (show_current)
        ui_draw_text(bx + bw - 20, by + 1, L" [running]");
    ui_reset_colors();

    int row = by + 2;
    int inner_w = bw - 4;
    const wchar_t *opnames[] = { L"?", L"Copy", L"Move", L"Delete" };

    /* current operation progress bar */
    if (show_current && bt && bt->active) {
        ui_set_bg(theme_get(theme, COLOR_DIALOG_BG));
        ui_set_fg(theme_get(theme, COLOR_SELECTED_FG));
        ui_set_bold();
        wchar_t buf[320];
        DWORD el = GetTickCount() - bt->start_ticks;
        int pct = bt->total_items ? (int)((int64_t)bt->done_items * 100 / bt->total_items) : 0;
        swprintf_s(buf, 320, L" %s  %d/%d  %d%%  %d:%02d  %s",
                   opnames[bt->op_type >= 1 && bt->op_type <= 3 ? bt->op_type : 0],
                   (int)bt->done_items, bt->total_items, pct,
                   (el/1000)/60, (el/1000)%60,
                   bt->current_file[0] ? bt->current_file : L"...");
        ui_draw_text_trunc(bx + 2, row, inner_w, buf);
        ui_reset_colors();
        row++;

        /* progress bar */
        ui_set_bg(theme_get(theme, COLOR_TAB_INACTIVE));
        ui_set_fg(theme_get(theme, COLOR_TAB_INACTIVE));
        ui_draw_h_line(bx + 2, row, inner_w, L'\x2591');
        if (bt->total_items > 0) {
            int fill = (int)(((int64_t)bt->done_items * (inner_w - 2)) / bt->total_items);
            if (fill < 0) fill = 0;
            if (fill > inner_w - 2) fill = inner_w - 2;
            ui_set_bg(theme_get(theme, COLOR_PROGRESS));
            ui_set_fg(theme_get(theme, COLOR_PROGRESS));
            ui_draw_h_line(bx + 3, row, fill, L'\x2588');
        }
        ui_reset_colors();
        row++;
    }

    /* divider */
    if (hist_count > 0) {
        ui_set_fg(theme_get(theme, COLOR_PANEL_BORDER));
        ui_set_bg(theme_get(theme, COLOR_DIALOG_BG));
        ui_draw_h_line(bx + 2, row, inner_w, L'\x2500');
        ui_reset_colors();
        row++;
    }

    /* history items */
    int start_hist = 0;
    if (hist_count > lines - row + by) start_hist = hist_count - (lines - row + by);
    if (start_hist < 0) start_hist = 0;
    for (int i = start_hist; i < hist_count && row < by + bh - 2; i++) {
        const Hr *h = &hist[i];
        wchar_t buf[256];
        const wchar_t *st = h->status ? L"OK" : L"FAIL";
        uint32_t sc = h->status ? theme_get(theme, COLOR_PROGRESS) : theme_get(theme, COLOR_ERROR);
        swprintf_s(buf, 256, L"   %-6s %d/%d  %s", opnames[h->op_type >= 1 && h->op_type <= 3 ? h->op_type : 0],
                   h->done, h->total, h->desc[0] ? h->desc : L"");
        ui_set_bg(theme_get(theme, COLOR_DIALOG_BG));
        ui_set_fg(theme_get(theme, COLOR_FILE));
        ui_set_dim();
        ui_draw_text(bx + 2, row, buf);
        ui_set_fg(sc);
        ui_set_bold();
        ui_draw_text(bx + inner_w - 6, row, st);
        row++;
        ui_reset_colors();
    }

    /* empty state */
    if (hist_count == 0 && !show_current) {
        ui_set_bg(theme_get(theme, COLOR_DIALOG_BG));
        ui_set_fg(theme_get(theme, COLOR_FILE));
        ui_set_dim();
        ui_draw_text(bx + 4, row, L"(no operations yet)");
        ui_reset_colors();
    }

    ui_set_bg(theme_get(theme, COLOR_DIALOG_BG));
    ui_set_fg(theme_get(theme, COLOR_DIALOG_BORDER));
    ui_draw_text_centered(by + bh - 1, bw, L" F3 / Esc = close ");
    ui_reset_colors();
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

    while (1) {
        ui_begin_frame();

        ui_set_bg(theme_get(theme, COLOR_BG));
        ui_clear_screen();
        ui_reset_colors();

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
        {
            const wchar_t *ft = L"Press any key to continue...";
            int fl = (int)wcslen(ft);
            ui_draw_text(bx + (bw - fl) / 2, by + bh - 2, ft);
        }
        ui_reset_colors();

        ui_end_frame();

        KeyEvent ev;
        while (input_poll(&ev)) {
            if (ev.code && ev.code != KEY_RESIZE) goto msg_done;
        }
    }
msg_done:
    ;
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

    int selected = 1;

    while (1) {
        ui_begin_frame();

        ui_set_bg(theme_get(theme, COLOR_BG));
        ui_clear_screen();
        ui_reset_colors();

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
        ui_end_frame();

        KeyEvent ev;
        while (input_poll(&ev)) {
            if (ev.code == KEY_RESIZE) continue;
            if (ev.code == KEY_LEFT || ev.code == KEY_RIGHT || ev.code == KEY_TAB) {
                selected = !selected;
                break;
            }
            if (ev.code == KEY_ENTER) return selected;
            if (ev.code == KEY_ESC) return 0;
            if (ev.code == KEY_CHAR) {
                if (ev.ch == L'y' || ev.ch == L'Y') return 1;
                if (ev.ch == L'n' || ev.ch == L'N') return 0;
            }
            if (ev.code == KEY_SPACE) break;
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
        ui_begin_frame();

        ui_set_bg(theme_get(theme, COLOR_BG));
        ui_clear_screen();
        ui_reset_colors();

        ui_set_bg(theme_get(theme, COLOR_DIALOG_BG));
        ui_set_fg(theme_get(theme, COLOR_FILE));
        ui_draw_rect_content(bx, by, bw, bh);
        ui_set_fg(theme_get(theme, COLOR_DIALOG_BORDER));
        ui_draw_rect(bx, by, bw, bh);

        ui_set_bg(theme_get(theme, COLOR_DIALOG_BG));
        ui_set_fg(theme_get(theme, COLOR_SELECTED_FG));
        ui_set_bold();
        ui_draw_text(bx + 2, by + 1, title);
        ui_reset_colors();

        ui_set_bg(theme_get(theme, COLOR_BG));
        ui_set_fg(theme_get(theme, COLOR_CMDLINE));
        ui_draw_h_line(bx + 2, by + 2, bw - 4, L' ');
        ui_move(bx + 3, by + 2);
        emit_sgr();
        buf_append(buf, len);

        if (cursor < len) {
            ui_move(bx + 3 + cursor, by + 2);
            ui_set_bg(theme_get(theme, COLOR_SELECTED_BG));
            ui_set_fg(theme_get(theme, COLOR_SELECTED_FG));
            emit_sgr();
            buf_append(&buf[cursor], 1);
        }
        ui_reset_colors();

        ui_set_bg(theme_get(theme, COLOR_DIALOG_BG));
        ui_set_fg(theme_get(theme, COLOR_DIALOG_BORDER));
        {
            const wchar_t *ft = L"Enter=confirm  Esc=cancel";
            int fl = (int)wcslen(ft);
            ui_draw_text(bx + (bw - fl) / 2, by + bh - 2, ft);
        }
        ui_reset_colors();

        ui_end_frame();

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
            if (ev.code == KEY_SPACE && len < buf_len - 1) {
                memmove(buf + cursor + 1, buf + cursor, (len - cursor + 1) * sizeof(wchar_t));
                buf[cursor] = L' ';
                cursor++; len++;
                break;
            }
            break;
        }
    }
}
