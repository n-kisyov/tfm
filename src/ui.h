#ifndef UI_H
#define UI_H

#include <wchar.h>
#include <stdint.h>
#include <windows.h>
#include "theme.h"

typedef struct {
    int x, y, w, h;
} Rect;

void ui_init(void);
void ui_shutdown(void);
void ui_begin_frame(void);
void ui_end_frame(void);
void ui_get_term_size(int *w, int *h);

void ui_set_fg(uint32_t rgb);
void ui_set_bg(uint32_t rgb);
void ui_reset_colors(void);
void ui_set_bold(void);
void ui_set_dim(void);
void ui_reverse(void);

void ui_move(int x, int y);
void ui_clear_line(int y);
void ui_clear_rect(int x, int y, int w, int h);
void ui_draw_text(int x, int y, const wchar_t *text);
void ui_draw_text_trunc(int x, int y, int max_w, const wchar_t *text);
void ui_draw_text_centered(int y, int max_w, const wchar_t *text);
void ui_draw_char(int x, int y, wchar_t ch);
void ui_draw_h_line(int x, int y, int w, wchar_t ch);
void ui_draw_v_line(int x, int y, int h, wchar_t ch);
void ui_draw_rect(int x, int y, int w, int h);
void ui_draw_rect_content(int x, int y, int w, int h);
void ui_fill_rect(int x, int y, int w, int h, wchar_t ch);

void ui_hide_cursor(void);
void ui_show_cursor(int x, int y);
void ui_clear_screen(void);
void ui_draw_progress(const Theme *theme, int tw, int th,
                      int done, int total, const wchar_t *title,
                      const wchar_t *file, DWORD elapsed_ms);

void ui_message_box(const Theme *theme, const wchar_t *title, const wchar_t *msg);
int  ui_confirm_dialog(const Theme *theme, const wchar_t *title, const wchar_t *msg);
int  ui_input_dialog(const Theme *theme, const wchar_t *title, wchar_t *buf, int buf_len);

#endif
