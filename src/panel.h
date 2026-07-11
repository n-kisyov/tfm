#ifndef PANEL_H
#define PANEL_H

#include <wchar.h>
#include "fs.h"
#include "theme.h"

#define PANEL_MAX_TABS 5

typedef struct {
    wchar_t path[520];
    wchar_t display_name[32];
    int     scroll_offset;
    int     cursor;
} PanelTab;

typedef struct {
    PanelTab  tabs[PANEL_MAX_TABS];
    int       tab_count;
    int       active_tab;

    FileEntry *entries;
    int        entry_count;
    int       *tagged;
    int        tagged_count;

    int        needs_refresh;
    int        sort_by;
    int        sort_reverse;
    int        show_hidden;
    int        dirty;

    int        in_drive_list;
    int        panel_idx;           /* 0=left, 1=right */
    const struct FsProvider *fs;    /* may differ per-panel (local vs ssh) */
    wchar_t    drive_paths[26][520];
    FileEntry  saved_entries[4096];
    int        saved_entry_count;
    int        saved_cursor;
    wchar_t    saved_path[520];
} Panel;

void panel_init(Panel *p, const wchar_t *start_dir);
void panel_free(Panel *p);

void panel_refresh(Panel *p, const FsProvider *fs);
void panel_enter_dir(Panel *p, const FsProvider *fs);
void panel_go_parent(Panel *p, const FsProvider *fs);
void panel_go_drives(Panel *p, const FsProvider *fs);
void panel_exit_drives(Panel *p, const FsProvider *fs);
void panel_enter_on_drive(Panel *p, const FsProvider *fs);
void panel_cursor_up(Panel *p);
void panel_cursor_down(Panel *p);
void panel_page_up(Panel *p, int page_h);
void panel_page_down(Panel *p, int page_h);
void panel_cursor_home(Panel *p);
void panel_cursor_end(Panel *p);
void panel_toggle_tag(Panel *p);
void panel_clear_tags(Panel *p);
int  panel_tagged_or_current(const Panel *p, FileEntry **out, int *count);

int  panel_tab_new(Panel *p);
int  panel_tab_close(Panel *p);
int  panel_tab_next(Panel *p);
int  panel_tab_prev(Panel *p);
void panel_tab_rename(Panel *p);

void panel_render(const Panel *p, const Theme *theme, int x, int y, int w, int h, int focused);
const wchar_t *panel_current_path(const Panel *p);
int panel_list_height(const Panel *p);
int panel_is_tagged(const Panel *p, int idx);

#endif
