#ifndef THEME_H
#define THEME_H

#include <stdint.h>

typedef enum {
    COLOR_BG,
    COLOR_PANEL_BORDER,
    COLOR_FOCUS_BORDER,
    COLOR_TAB_ACTIVE,
    COLOR_TAB_INACTIVE,
    COLOR_TAB_ACTIVE_BG,
    COLOR_DIR,
    COLOR_FILE,
    COLOR_SYMLINK,
    COLOR_SELECTED_BG,
    COLOR_SELECTED_FG,
    COLOR_TAGGED,
    COLOR_TAGGED_BG,
    COLOR_STATUS_BAR,
    COLOR_STATUS_BG,
    COLOR_STATUS_FG,
    COLOR_CMDLINE,
    COLOR_DIALOG_BORDER,
    COLOR_DIALOG_BG,
    COLOR_ERROR,
    COLOR_PROGRESS,
    COLOR_COUNT
} ColorRole;

typedef struct {
    char     name[64];
    uint32_t colors[COLOR_COUNT];
} Theme;

void     theme_set_default(Theme *t);
int      theme_load(Theme *t, const wchar_t *path);
uint32_t theme_get(const Theme *t, ColorRole role);
void     theme_ansi_fg(char *buf, size_t sz, uint32_t rgb);
void     theme_ansi_bg(char *buf, size_t sz, uint32_t rgb);
void     theme_ansi_reset(char *buf, size_t sz);

#endif
