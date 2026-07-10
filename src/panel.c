#include "panel.h"
#include "ui.h"
#include "utils.h"
#include <stdlib.h>
#include <stdio.h>

void panel_init(Panel *p, const wchar_t *start_dir) {
    memset(p, 0, sizeof(Panel));
    p->tab_count = 1;
    p->active_tab = 0;
    wcscpy_s(p->tabs[0].path, 520, start_dir);
    const wchar_t *name = wcsrchr(start_dir, L'\\');
    wcsncpy_s(p->tabs[0].display_name, 32, name ? name + 1 : start_dir, 31);
    p->tabs[0].cursor = 0;
    p->tabs[0].scroll_offset = 0;
    p->needs_refresh = 1;
    p->dirty = 1;
    p->sort_by = 0;
    p->sort_reverse = 0;
    p->show_hidden = 0;
}

void panel_free(Panel *p) {
    for (int i = 0; i < p->tab_count; i++) {
        /* nothing to free per-tab */
    }
    if (p->entries) {
        free(p->entries);
        p->entries = NULL;
    }
    free(p->tagged);
    p->tagged = NULL;
    p->tagged_count = 0;
    p->entry_count = 0;
}

void panel_refresh(Panel *p, const FsProvider *fs) {
    PanelTab *tab = &p->tabs[p->active_tab];
    if (p->entries) {
        free(p->entries);
        p->entries = NULL;
    }
    p->entry_count = 0;
    if (p->tagged) free(p->tagged);
    p->tagged = NULL;
    p->tagged_count = 0;

    fs->list_dir(tab->path, &p->entries, &p->entry_count);
    if (!p->entries || p->entry_count == 0) {
        p->entry_count = 0;
        p->entries = NULL;
    } else {
        fs_entries_sort(p->entries, p->entry_count, p->sort_by, p->sort_reverse);
        p->entry_count = p->entry_count;
    }
    tab->cursor = 0;
    tab->scroll_offset = 0;
    p->needs_refresh = 0;
    p->dirty = 1;
}

void panel_enter_dir(Panel *p, const FsProvider *fs) {
    PanelTab *tab = &p->tabs[p->active_tab];
    if (p->entry_count == 0) return;
    FileEntry *e = &p->entries[tab->cursor];
    if (e->type == ENTRY_DIR) {
        wchar_t *new_path = path_join(tab->path, e->name);
        wcscpy_s(tab->path, 520, new_path);
        wcsncpy_s(tab->display_name, 32, e->name, 31);
        free(new_path);
        /* remember drive path */
        wchar_t drive = towupper(tab->path[0]);
        if (drive >= L'A' && drive <= L'Z')
            wcscpy_s(p->drive_paths[drive - L'A'], 520, tab->path);
        tab->cursor = 0;
        tab->scroll_offset = 0;
        panel_refresh(p, fs);
    }
}

void panel_go_parent(Panel *p, const FsProvider *fs) {
    if (p->in_drive_list) { panel_exit_drives(p, fs); return; }
    PanelTab *tab = &p->tabs[p->active_tab];
    if (is_root_path(tab->path)) { panel_go_drives(p, fs); return; }
    wchar_t *parent = get_parent_path(tab->path);
    wcscpy_s(tab->path, 520, parent);
    const wchar_t *name = wcsrchr(parent, L'\\');
    if (!name) name = parent;
    if (name[0] == L'\\') name++;
    wcsncpy_s(tab->display_name, 32, name && name[0] ? name : parent, 31);
    free(parent);
    wchar_t drive = towupper(tab->path[0]);
    if (drive >= L'A' && drive <= L'Z')
        wcscpy_s(p->drive_paths[drive - L'A'], 520, tab->path);
    tab->cursor = 0;
    tab->scroll_offset = 0;
    panel_refresh(p, fs);
}

void panel_go_drives(Panel *p, const FsProvider *fs) {
    if (p->in_drive_list) {
        panel_exit_drives(p, fs);
        return;
    }
    PanelTab *tab = &p->tabs[p->active_tab];
    (void)fs;

    /* save current path for current drive letter */
    wchar_t drive = towupper(tab->path[0]);
    if (drive >= L'A' && drive <= L'Z')
        wcscpy_s(p->drive_paths[drive - L'A'], 520, tab->path);

    /* save current entries to restore later */
    p->saved_entry_count = p->entry_count;
    p->saved_cursor = tab->cursor;
    wcscpy_s(p->saved_path, 520, tab->path);
    if (p->saved_entry_count > 0 && p->saved_entry_count <= 4096)
        memcpy(p->saved_entries, p->entries, p->saved_entry_count * sizeof(FileEntry));

    /* build drive list */
    free(p->entries);
    p->entries = (FileEntry *)calloc(26 + 1, sizeof(FileEntry));
    p->entry_count = 0;
    p->in_drive_list = 1;

    DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; i++) {
        if (drives & (1 << i)) {
            wchar_t dl = (wchar_t)(L'A' + i);
            if (p->drive_paths[i][0]) {
                swprintf_s(p->entries[p->entry_count].name, 256,
                           L"%c:\\    (%s)", dl, p->drive_paths[i]);
            } else {
                swprintf_s(p->entries[p->entry_count].name, 256, L"%c:\\", dl);
            }
            p->entries[p->entry_count].type = ENTRY_DIR;
            p->entry_count++;
        }
    }

    tab->cursor = 0;
    tab->scroll_offset = 0;
    wcscpy_s(tab->path, 520, L"Drives");
    wcsncpy_s(tab->display_name, 32, L"Drives", 31);
    p->dirty = 1;
}

void panel_exit_drives(Panel *p, const FsProvider *fs) {
    (void)fs;
    PanelTab *tab = &p->tabs[p->active_tab];
    free(p->entries);
    p->in_drive_list = 0;

    /* restore saved entries */
    p->entry_count = p->saved_entry_count;
    wcscpy_s(tab->path, 520, p->saved_path);
    const wchar_t *nm = wcsrchr(p->saved_path, L'\\');
    wcsncpy_s(tab->display_name, 32, nm ? nm + 1 : p->saved_path, 31);

    if (p->saved_entry_count > 0) {
        p->entries = (FileEntry *)calloc(p->saved_entry_count, sizeof(FileEntry));
        memcpy(p->entries, p->saved_entries, p->saved_entry_count * sizeof(FileEntry));
        tab->cursor = p->saved_cursor;
    } else {
        p->entries = NULL;
        tab->cursor = 0;
    }
    tab->scroll_offset = 0;
    p->dirty = 1;
}

void panel_enter_on_drive(Panel *p, const FsProvider *fs) {
    PanelTab *tab = &p->tabs[p->active_tab];
    if (!p->in_drive_list || p->entry_count == 0) return;
    FileEntry *e = &p->entries[tab->cursor];
    wchar_t drive_letter = towupper(e->name[0]);

    p->in_drive_list = 0;
    free(p->entries);
    p->entries = NULL;
    p->entry_count = 0;

    if (p->drive_paths[drive_letter - L'A'][0] &&
        fs->exists(p->drive_paths[drive_letter - L'A']) &&
        fs->is_dir(p->drive_paths[drive_letter - L'A'])) {
        wcscpy_s(tab->path, 520, p->drive_paths[drive_letter - L'A']);
    } else {
        swprintf_s(tab->path, 520, L"%c:\\", drive_letter);
        p->drive_paths[drive_letter - L'A'][0] = 0; /* clear invalid path */
    }
    const wchar_t *nm = wcsrchr(tab->path, L'\\');
    wcsncpy_s(tab->display_name, 32, nm ? nm + 1 : tab->path, 31);
    tab->cursor = 0;
    tab->scroll_offset = 0;
    panel_refresh(p, fs);
}

void panel_cursor_up(Panel *p) {
    PanelTab *tab = &p->tabs[p->active_tab];
    if (p->entry_count == 0) return;
    if (tab->cursor > 0) {
        tab->cursor--;
        if (tab->cursor < tab->scroll_offset)
            tab->scroll_offset = tab->cursor;
        p->dirty = 1;
    }
}

void panel_cursor_down(Panel *p) {
    PanelTab *tab = &p->tabs[p->active_tab];
    if (p->entry_count == 0) return;
    if (tab->cursor < p->entry_count - 1) {
        tab->cursor++;
        p->dirty = 1;
    }
}

void panel_page_up(Panel *p, int page_h) {
    PanelTab *tab = &p->tabs[p->active_tab];
    if (p->entry_count == 0) return;
    tab->cursor -= page_h;
    if (tab->cursor < 0) tab->cursor = 0;
    tab->scroll_offset -= page_h;
    if (tab->scroll_offset < 0) tab->scroll_offset = 0;
    p->dirty = 1;
}

void panel_page_down(Panel *p, int page_h) {
    PanelTab *tab = &p->tabs[p->active_tab];
    if (p->entry_count == 0) return;
    tab->cursor += page_h;
    if (tab->cursor >= p->entry_count) tab->cursor = p->entry_count - 1;
    tab->scroll_offset += page_h;
    p->dirty = 1;
}

void panel_cursor_home(Panel *p) {
    PanelTab *tab = &p->tabs[p->active_tab];
    tab->cursor = 0;
    tab->scroll_offset = 0;
    p->dirty = 1;
}

void panel_cursor_end(Panel *p) {
    PanelTab *tab = &p->tabs[p->active_tab];
    if (p->entry_count > 0) tab->cursor = p->entry_count - 1;
    p->dirty = 1;
}

void panel_toggle_tag(Panel *p) {
    PanelTab *tab = &p->tabs[p->active_tab];
    if (p->entry_count == 0) return;
    int idx = tab->cursor;

    /* check if already tagged */
    for (int i = 0; i < p->tagged_count; i++) {
        if (p->tagged[i] == idx) {
            /* remove */
            for (int j = i; j < p->tagged_count - 1; j++)
                p->tagged[j] = p->tagged[j + 1];
            p->tagged_count--;
            p->dirty = 1;
            return;
        }
    }
    /* add */
    p->tagged = (int *)xrealloc(p->tagged, (p->tagged_count + 1) * sizeof(int));
    p->tagged[p->tagged_count++] = idx;
    p->dirty = 1;
}

void panel_clear_tags(Panel *p) {
    free(p->tagged);
    p->tagged = NULL;
    p->tagged_count = 0;
    p->dirty = 1;
}

int panel_tagged_or_current(const Panel *p, FileEntry **out, int *count) {
    if (p->tagged_count > 0) {
        *count = p->tagged_count;
        *out = (FileEntry *)malloc(p->tagged_count * sizeof(FileEntry));
        for (int i = 0; i < p->tagged_count; i++) {
            (*out)[i] = p->entries[p->tagged[i]];
        }
        return 1;
    }
    if (p->entry_count > 0) {
        *count = 1;
        *out = (FileEntry *)malloc(sizeof(FileEntry));
        (*out)[0] = p->entries[p->tabs[p->active_tab].cursor];
        return 1;
    }
    *count = 0;
    *out = NULL;
    return 0;
}

int panel_tab_new(Panel *p) {
    if (p->tab_count >= PANEL_MAX_TABS) return 0;
    PanelTab *nt = &p->tabs[p->tab_count];
    memset(nt, 0, sizeof(PanelTab));
    wchar_t *home = get_home_dir();
    wcscpy_s(nt->path, 520, home);
    wcsncpy_s(nt->display_name, 32, L"Home", 31);
    nt->cursor = 0;
    p->active_tab = p->tab_count;
    p->tab_count++;
    p->needs_refresh = 1;
    p->dirty = 1;
    return 1;
}

int panel_tab_close(Panel *p) {
    if (p->tab_count <= 1) return 0;
    int idx = p->active_tab;
    /* shift tabs left */
    for (int i = idx; i < p->tab_count - 1; i++) {
        p->tabs[i] = p->tabs[i + 1];
    }
    p->tab_count--;
    if (p->active_tab >= p->tab_count) p->active_tab = p->tab_count - 1;
    p->needs_refresh = 1;
    p->dirty = 1;
    return 1;
}

int panel_tab_next(Panel *p) {
    if (p->tab_count <= 1) return 0;
    p->active_tab = (p->active_tab + 1) % p->tab_count;
    p->needs_refresh = 1;
    p->dirty = 1;
    return 1;
}

int panel_tab_prev(Panel *p) {
    if (p->tab_count <= 1) return 0;
    p->active_tab--;
    if (p->active_tab < 0) p->active_tab = p->tab_count - 1;
    p->needs_refresh = 1;
    p->dirty = 1;
    return 1;
}

void panel_tab_rename(Panel *p) {
    PanelTab *tab = &p->tabs[p->active_tab];
    const wchar_t *name = wcsrchr(tab->path, L'\\');
    if (!name) name = tab->path;
    if (name[0] == L'\\') name++;
    wcsncpy_s(tab->display_name, 32, name, 31);
    p->dirty = 1;
}

const wchar_t *panel_current_path(const Panel *p) {
    return p->tabs[p->active_tab].path;
}

int panel_list_height(const Panel *p) {
    return p->entry_count;
}

int panel_is_tagged(const Panel *p, int idx) {
    for (int i = 0; i < p->tagged_count; i++) {
        if (p->tagged[i] == idx) return 1;
    }
    return 0;
}

void panel_render(const Panel *p, const Theme *theme, int x, int y, int w, int h, int focused) {
    uint32_t border_color = focused
        ? theme_get(theme, COLOR_FOCUS_BORDER)
        : theme_get(theme, COLOR_PANEL_BORDER);

    PanelTab *tab = (PanelTab *)&p->tabs[p->active_tab];

    /* fill the entire panel area (including border cells) with background */
    ui_set_bg(theme_get(theme, COLOR_BG));
    for (int iy = 0; iy < h; iy++) {
        ui_draw_h_line(x, y + iy, w, L' ');
    }

    /* panel border drawn on top of fill, with explicit background */
    ui_set_bg(theme_get(theme, COLOR_BG));
    ui_set_fg(border_color);
    ui_draw_rect(x, y, w, h);

    /* tab bar row (y+1) */
    ui_set_bg(theme_get(theme, COLOR_BG));
    for (int ti = 0; ti < p->tab_count && ti < PANEL_MAX_TABS; ti++) {
        int tab_x = x + 2 + ti * 20;
        if (tab_x >= x + w - 4) break;
        int tab_w = 18;
        if (tab_x + tab_w > x + w - 2) tab_w = (x + w - 2) - tab_x;
        if (tab_w < 3) break;

        if (ti == p->active_tab) {
            ui_set_bg(theme_get(theme, COLOR_TAB_ACTIVE_BG));
            ui_set_fg(theme_get(theme, COLOR_TAB_ACTIVE));
            ui_set_bold();
        } else {
            ui_set_bg(theme_get(theme, COLOR_BG));
            ui_set_fg(theme_get(theme, COLOR_TAB_INACTIVE));
        }

        wchar_t tab_label[20];
        const wchar_t *tname = p->tabs[ti].display_name;
        if (!tname || tname[0] == 0) tname = L"?";
        int tlen = (int)wcslen(tname);
        if (tlen > tab_w - 4) {
            wcsncpy_s(tab_label, 20, tname, tab_w - 4);
            tab_label[tab_w - 4] = 0;
            wcscat_s(tab_label, 20, L"...");
        } else {
            swprintf_s(tab_label, 20, L" %d:%-*s ", ti + 1, tab_w - 6, tname);
        }
        ui_draw_text(tab_x, y + 1, tab_label);
        ui_reset_colors();
    }

    /* path display line (y+2) */
    ui_set_bg(theme_get(theme, COLOR_BG));
    if (p->in_drive_list) {
        ui_set_fg(theme_get(theme, COLOR_FOCUS_BORDER));
        ui_set_bold();
        ui_draw_text_trunc(x + 2, y + 2, w - 4, L"\x25b6 Select Drive (Enter=switch  Esc=back)");
    } else {
        ui_set_fg(theme_get(theme, COLOR_FILE));
        ui_set_dim();
        ui_draw_text_trunc(x + 2, y + 2, w - 4, tab->path);
    }
    ui_reset_colors();

    int list_start_y = y + 3;
    int list_h = h - 5; /* overhead: top-border + tab-bar + path + status + bottom-border = 5 */
    if (list_h < 1) list_h = 1;

    /* adjust scroll to keep cursor visible */
    if (tab->cursor < tab->scroll_offset)
        tab->scroll_offset = tab->cursor;
    if (tab->cursor >= tab->scroll_offset + list_h)
        tab->scroll_offset = tab->cursor - list_h + 1;
    if (tab->scroll_offset < 0) tab->scroll_offset = 0;

    for (int i = 0; i < list_h; i++) {
        int ei = tab->scroll_offset + i;
        int ry = list_start_y + i;
        int rx = x + 1;
        ui_move(rx, ry);

        if (ei >= p->entry_count) {
            ui_set_bg(theme_get(theme, COLOR_BG));
            ui_set_fg(theme_get(theme, COLOR_BG));
            ui_draw_h_line(rx, ry, w - 2, L' ');
            ui_reset_colors();
            continue;
        }

        FileEntry *e = &p->entries[ei];
        int is_tagged = panel_is_tagged(p, ei);
        int is_cursor = (ei == tab->cursor);

        if (is_cursor) {
            ui_set_bg(theme_get(theme, COLOR_SELECTED_BG));
            ui_set_fg(theme_get(theme, COLOR_SELECTED_FG));
        } else if (is_tagged) {
            ui_set_bg(theme_get(theme, COLOR_TAGGED_BG));
        } else {
            ui_set_bg(theme_get(theme, COLOR_BG));
        }

        /* tag indicator */
        if (is_tagged) {
            ui_set_fg(theme_get(theme, COLOR_TAGGED));
            ui_draw_char(rx, ry, L'✓');
        } else {
            ui_draw_char(rx, ry, L' ');
        }
        rx++;

        /* entry name coloring */
        if (is_cursor) {
            ui_set_fg(theme_get(theme, COLOR_SELECTED_FG));
        } else if (e->type == ENTRY_DIR) {
            ui_set_fg(theme_get(theme, COLOR_DIR));
        } else if (e->type == ENTRY_SYMLINK) {
            ui_set_fg(theme_get(theme, COLOR_SYMLINK));
        } else {
            ui_set_fg(theme_get(theme, COLOR_FILE));
        }

        if (e->type == ENTRY_FILE) {
            const wchar_t *ext = get_file_ext(e->name);
            if (ext && (_wcsicmp(ext, L"exe") == 0 || _wcsicmp(ext, L"bat") == 0 ||
                        _wcsicmp(ext, L"cmd") == 0 || _wcsicmp(ext, L"ps1") == 0 ||
                        _wcsicmp(ext, L"com") == 0)) {
                if (!is_cursor) ui_set_fg(theme_get(theme, COLOR_PROGRESS));
            }
        }

        /* name */
        wchar_t size_buf[32];
        int name_w = w - 22; /* leave room for size + date */
        if (name_w < 10) name_w = 10;

        int is_dir = (e->type == ENTRY_DIR);
        wchar_t name_display[512];
        if (is_dir) {
            swprintf_s(name_display, 512, L" %s/", e->name);
        } else {
            swprintf_s(name_display, 512, L" %s", e->name);
        }
        ui_draw_text_trunc(rx, ry, name_w, name_display);

        /* size (right-aligned) */
        int size_x = x + w - 22;
        format_file_size(e->size, size_buf, 32);
        if (is_dir) {
            swprintf_s(size_buf, 32, L"<DIR>");
        }
        ui_set_fg(is_cursor ? theme_get(theme, COLOR_SELECTED_FG) : theme_get(theme, COLOR_FILE));
        ui_set_dim();
        ui_draw_text(size_x, ry, size_buf);

        ui_reset_colors();
    }

    /* status line at bottom of panel */
    int status_y = y + h - 2;
    ui_set_bg(theme_get(theme, COLOR_BG));
    ui_set_fg(theme_get(theme, COLOR_FILE));
    ui_set_dim();
    wchar_t info[256];
    swprintf_s(info, 256, L" %d/%d files ", p->tagged_count, p->entry_count);
    ui_draw_text(x + 2, status_y, info);

    /* scroll indicator */
    if (p->entry_count > list_h) {
        int pct = 0;
        if (p->entry_count > 0) pct = (tab->cursor * 100) / p->entry_count;
        swprintf_s(info, 256, L" %d%% ", pct);
        int info_x = x + w - (int)wcslen(info) - 3;
        if (info_x > x + 20) ui_draw_text(info_x, status_y, info);
    }
    ui_reset_colors();
}
