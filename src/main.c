#include "main.h"
#include "ui.h"
#include "input.h"
#include "ops.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>

AppState g_app;

static int panel_h_for(int idx);

static void init_app(void) {
    memset(&g_app, 0, sizeof(g_app));

    ui_init();
    input_init();

    const wchar_t *cfg_path = config_get_path();
    config_set_defaults(&g_app.config);

    /* try to load config */
    if (!config_load(&g_app.config, cfg_path)) {
        /* first run - save defaults */
        config_save(&g_app.config, cfg_path);
    }

    /* load theme */
    wchar_t theme_full[520];
    if (g_app.config.theme_path[0]) {
        /* try config path directly first */
        if (!theme_load(&g_app.theme, g_app.config.theme_path)) {
            /* try relative to themes/ dir */
            wchar_t *fname = wcsrchr(g_app.config.theme_path, L'\\');
            if (!fname) fname = wcsrchr(g_app.config.theme_path, L'/');
            if (fname) fname++; else fname = (wchar_t *)g_app.config.theme_path;
            swprintf_s(theme_full, 520, L"themes\\%s", fname);
            if (!theme_load(&g_app.theme, theme_full)) {
                theme_set_default(&g_app.theme);
            }
        }
    } else {
        theme_set_default(&g_app.theme);
    }

    g_app.fs = &fs_local;

    /* init panels */
    for (int i = 0; i < 2; i++) {
        wchar_t *dir = g_app.config.startup_dirs[i][0];
        if (!dir || dir[0] == 0 || !g_app.fs->exists(dir) || !g_app.fs->is_dir(dir))
            dir = get_home_dir();
        panel_init(&g_app.panels[i], dir);

        /* add extra startup tabs */
        for (int ti = 1; ti < g_app.config.startup_tab_counts[i]; ti++) {
            g_app.panels[i].tab_count++;
            PanelTab *pt = &g_app.panels[i].tabs[ti];
            wchar_t *tp = g_app.config.startup_dirs[i][ti];
            if (!tp || !tp[0] || !g_app.fs->exists(tp) || !g_app.fs->is_dir(tp))
                tp = get_home_dir();
            wcscpy_s(pt->path, 520, tp);
            const wchar_t *nm = wcsrchr(pt->path, L'\\');
            wcsncpy_s(pt->display_name, 32, nm ? nm + 1 : pt->path, 31);
        }

        g_app.panels[i].show_hidden = g_app.config.show_hidden;
        g_app.panels[i].sort_by = g_app.config.sort_by;
        g_app.panels[i].sort_reverse = g_app.config.sort_reverse;
        for (int d = 0; d < 26; d++) {
            wchar_t *dp = g_app.config.drive_paths[i][d];
            if (dp[0] && g_app.fs->exists(dp) && g_app.fs->is_dir(dp))
                wcscpy_s(g_app.panels[i].drive_paths[d], 520, dp);
        }
        panel_refresh(&g_app.panels[i], g_app.fs);
    }

    cmdline_init(&g_app.cmdline);
    g_app.focus = FOCUS_LEFT;

    ui_get_term_size(&g_app.tw, &g_app.th);
    g_app.left_w = (g_app.tw * g_app.config.panel_split_pct) / 100;
    if (g_app.left_w < 20) g_app.left_w = 20;
    if (g_app.left_w > g_app.tw - 20) g_app.left_w = g_app.tw - 20;

    g_app.running = 1;
    g_app.needs_redraw = 1;
    bgop_init(&g_app.bgtask);
}

static void shutdown_app(void) {
    g_app.running = 0;

    /* save current panel state — do this BEFORE bgop_free in case bgop holds a CS */
    for (int i = 0; i < 2; i++) {
        Panel *p = &g_app.panels[i];
        g_app.config.startup_tab_counts[i] = p->tab_count;
        for (int ti = 0; ti < p->tab_count && ti < MAX_TABS; ti++) {
            const wchar_t *save_path = p->tabs[ti].path;
            if (p->in_drive_list && wcscmp(save_path, L"Drives") == 0)
                save_path = p->saved_path;
            wcscpy_s(g_app.config.startup_dirs[i][ti], CONFIG_MAX_PATH, save_path);
        }
        for (int d = 0; d < 26; d++)
            wcscpy_s(g_app.config.drive_paths[i][d], CONFIG_MAX_PATH, p->drive_paths[d]);
    }
    config_save(&g_app.config, config_get_path());

    bgop_free(&g_app.bgtask);
    for (int i = 0; i < 2; i++) panel_free(&g_app.panels[i]);
    cmdline_free(&g_app.cmdline);

    input_shutdown();
    ui_shutdown();
}

static void render(void) {
    int tw = g_app.tw, th = g_app.th;
    int lw = g_app.left_w;
    int panel_start_y = 1;       /* row 0 = function key bar */
    int panel_h = th - 2;        /* panels fill rows 1..th-2, cmdline on th-1 */
    if (panel_h < 6) { panel_h = 6; }

    ui_begin_frame();
    ui_hide_cursor();

    ui_set_bg(theme_get(&g_app.theme, COLOR_BG));
    ui_clear_screen();
    ui_reset_colors();

    /* ---------- Help screen overlay ---------- */
    if (g_app.show_help) {
        static const wchar_t *help[] = {
            L"       Navigation                         File Ops",
            L"       ---------                         --------",
            L"       Arrows    Move cursor             F5   Copy to opposite panel",
            L"       PgUp/Dn   Page up/down            F6   Move to opposite panel",
            L"       Home/End  First / last entry      F7   Create directory",
            L"       Enter     Enter dir / open file   F8   Delete (with confirm)",
            L"       Backspace Parent directory        Space Toggle tag (multi-select)",
            L"       Ctrl+D    Drive selector          F3   View / Re-show progress",
            L"       Esc       Clear tags              F2   Refresh panel",
            L"",
            L"       Tabs                                Shell line",
            L"       ----                                ----------",
            L"       Ctrl+T    New tab                  Arrows  Command history",
            L"       Ctrl+W    Close tab                Enter   Execute command",
            L"       Ctrl+Tab  Next tab                 Esc     Clear command line",
            L"       Tab       Rotate focus             Backsp  Delete previous char",
            L"",
            L"                     F1 Help    F12 Exit",
            NULL
        };

        int hc = 0, maxw = 0;
        while (help[hc]) {
            int lw = (int)wcslen(help[hc]);
            if (lw > maxw) maxw = lw;
            hc++;
        }
        int bw = maxw + 6;
        if (bw > tw - 2) bw = tw - 2;
        int bh = hc + 4;
        int bx = (tw - bw) / 2;
        int by = (th - bh) / 2;
        if (by < 0) by = 0;
        if (by + bh >= th) by = th - bh;

        /* frame */
        ui_set_bg(theme_get(&g_app.theme, COLOR_DIALOG_BG));
        ui_set_fg(theme_get(&g_app.theme, COLOR_FILE));
        ui_fill_rect(bx, by, bw, bh, L' ');
        ui_reset_colors();
        ui_set_fg(theme_get(&g_app.theme, COLOR_FOCUS_BORDER));
        ui_draw_rect(bx, by, bw, bh);

        /* title */
        ui_set_bg(theme_get(&g_app.theme, COLOR_DIALOG_BG));
        ui_set_fg(theme_get(&g_app.theme, COLOR_SELECTED_FG));
        ui_set_bold();
        ui_draw_text_centered(by + 1, bw, L" tfm Keyboard Shortcuts ");
        ui_reset_colors();

        /* content */
        ui_set_bg(theme_get(&g_app.theme, COLOR_DIALOG_BG));
        for (int i = 0; help[i]; i++) {
            int row = by + 3 + i;
            if (row >= by + bh - 1) break;
            if (help[i][0]) {
                ui_set_fg(theme_get(&g_app.theme, COLOR_FILE));
                ui_draw_text(bx + 3, row, help[i]);
            }
        }

        /* footer */
        ui_set_fg(theme_get(&g_app.theme, COLOR_DIALOG_BORDER));
        ui_draw_text_centered(by + bh - 2, bw, L" F1 = Close ");

        ui_reset_colors();
        ui_end_frame();
        return;
    }

    /* ---------- row 0: function-key bar at the top ---------- */
    ui_set_bg(theme_get(&g_app.theme, COLOR_STATUS_BG));
    ui_set_fg(theme_get(&g_app.theme, COLOR_STATUS_FG));
    ui_fill_rect(0, 0, tw, 1, L' ');

    int total_tagged = g_app.panels[0].tagged_count + g_app.panels[1].tagged_count;
    int bgop_running = bgop_is_active(&g_app.bgtask);
    wchar_t keybar[320];

    if (bgop_running) {
        if (total_tagged > 0)
            swprintf_s(keybar, 320, L" F2Rfrsh  F3Prog  F5Copy  F6Move  F8Del  F12Quit  Tag:%d",
                       total_tagged);
        else
            swprintf_s(keybar, 320, L" F2Rfrsh  F3Prog  F5Copy  F6Move  F8Del  F12Quit");
    } else if (total_tagged > 0) {
        swprintf_s(keybar, 320, L" F2Rfrsh  F3View  F5Copy  F6Move  F7Mkdir  F8Del  F12Quit  Tag:%d",
                   total_tagged);
    } else {
        swprintf_s(keybar, 320, L" F2Rfrsh  F3View  F5Copy  F6Move  F7Mkdir  F8Del  F12Quit");
    }
    ui_draw_text_trunc(0, 0, tw, keybar);

    /* bgop status at the right end with different colour */
    if (bgop_running) {
        const wchar_t *opnames[] = { L"?", L"Copy", L"Move", L"Delete" };
        int op = (g_app.bgtask.op_type >= 1 && g_app.bgtask.op_type <= 3) ? g_app.bgtask.op_type : 0;
        const wchar_t *vis = g_app.bgtask.visible ? L"" : L" [hidden]";
        wchar_t bgop_str[80];
        swprintf_s(bgop_str, 80, L" \x25b6%s %d/%d%s  ",
                   opnames[op], (int)g_app.bgtask.done_items, g_app.bgtask.total_items, vis);
        int blen = (int)wcslen(bgop_str);
        int bx = tw - blen;
        if (bx < 40) bx = 40;
        ui_set_bg(theme_get(&g_app.theme, COLOR_PROGRESS));
        ui_set_fg(theme_get(&g_app.theme, COLOR_BG));
        ui_set_bold();
        ui_draw_text(bx, 0, bgop_str);
        ui_reset_colors();
    }

    /* ---------- panels ---------- */
    panel_render(&g_app.panels[0], &g_app.theme, 0, panel_start_y, lw, panel_h,
                 g_app.focus == FOCUS_LEFT);
    panel_render(&g_app.panels[1], &g_app.theme, lw, panel_start_y, tw - lw, panel_h,
                 g_app.focus == FOCUS_RIGHT);

    /* ---------- progress overlay (if active and visible) ---------- */
    if (bgop_is_active(&g_app.bgtask) && g_app.bgtask.visible) {
        DWORD elapsed = GetTickCount() - g_app.bgtask.start_ticks;
        ui_draw_progress(&g_app.theme, tw, th,
                         g_app.bgtask.done_items,
                         g_app.bgtask.total_items,
                         g_app.bgtask.title,
                         g_app.bgtask.current_file[0] ? g_app.bgtask.current_file : L"...",
                         elapsed);
    }

    /* ---------- command line (bottom row) ---------- */
    int cmd_y = th - 1;
    cmdline_render(&g_app.cmdline, &g_app.theme, 0, cmd_y, tw,
                   g_app.focus == FOCUS_CMDLINE);

    if (g_app.focus == FOCUS_CMDLINE) {
        ui_show_cursor(2 + g_app.cmdline.cursor, cmd_y);
    }

    ui_reset_colors();
    ui_end_frame();
}

static void handle_panel_input(Panel *panel, int panel_idx, KeyEvent *ev) {
    if (ev->code == KEY_RESIZE) {
        g_app.needs_redraw = 1;
        return;
    }

    if (bgop_is_active(&g_app.bgtask) && ev->code == KEY_ESC) {
        g_app.bgtask.visible = 0;
        g_app.needs_redraw = 1;
        return;
    }
    if (bgop_is_active(&g_app.bgtask) && ev->code == KEY_F3) {
        g_app.bgtask.visible = !g_app.bgtask.visible;
        g_app.needs_redraw = 1;
        return;
    }

    /* first check cmdline output dismiss */
    if (g_app.cmdline.show_output) {
        g_app.cmdline.show_output = 0;
        free(g_app.cmdline.last_output);
        g_app.cmdline.last_output = NULL;
        g_app.needs_redraw = 1;
        return;
    }

    /* ensure panel is refreshed */
    if (panel->needs_refresh) {
        panel_refresh(panel, g_app.fs);
    }

    /* tab management */
    switch (ev->code) {
    case KEY_F1:
        g_app.show_help = !g_app.show_help;
        g_app.needs_redraw = 1;
        return;
    case KEY_CTRL_T:
        panel_tab_new(panel);
        panel_refresh(panel, g_app.fs);
        g_app.needs_redraw = 1;
        return;
    case KEY_CTRL_W:
        panel_tab_close(panel);
        panel_refresh(panel, g_app.fs);
        g_app.needs_redraw = 1;
        return;
    case KEY_CTRL_TAB:
        panel_tab_next(panel);
        panel_refresh(panel, g_app.fs);
        g_app.needs_redraw = 1;
        return;
    case KEY_CTRL_SHIFT_TAB:
        panel_tab_prev(panel);
        panel_refresh(panel, g_app.fs);
        g_app.needs_redraw = 1;
        return;
    case KEY_CTRL_R:
        panel_refresh(panel, g_app.fs);
        g_app.needs_redraw = 1;
        return;
    case KEY_CTRL_D:
        panel_go_drives(panel, g_app.fs);
        g_app.needs_redraw = 1;
        return;
    default:
        break;
    }

    /* navigation */
    switch (ev->code) {
    case KEY_UP:
        panel_cursor_up(panel);
        g_app.needs_redraw = 1;
        break;
    case KEY_DOWN:
        panel_cursor_down(panel);
        g_app.needs_redraw = 1;
        break;
    case KEY_PAGEUP:
        panel_page_up(panel, panel_h_for(panel_idx));
        g_app.needs_redraw = 1;
        break;
    case KEY_PAGEDOWN:
        panel_page_down(panel, panel_h_for(panel_idx));
        g_app.needs_redraw = 1;
        break;
    case KEY_HOME:
        panel_cursor_home(panel);
        g_app.needs_redraw = 1;
        break;
    case KEY_END:
        panel_cursor_end(panel);
        g_app.needs_redraw = 1;
        break;
    case KEY_ENTER:
        if (panel->in_drive_list) {
            panel_enter_on_drive(panel, g_app.fs);
        } else if (panel->entry_count > 0) {
            PanelTab *tab = &panel->tabs[panel->active_tab];
            FileEntry *e = &panel->entries[tab->cursor];
            if (e->type == ENTRY_DIR) {
                panel_enter_dir(panel, g_app.fs);
            } else {
                wchar_t *full = path_join(tab->path, e->name);
                ShellExecuteW(NULL, L"open", full, NULL, NULL, SW_SHOW);
                free(full);
            }
        }
        g_app.needs_redraw = 1;
        break;
    case KEY_BACKSPACE:
        panel_go_parent(panel, g_app.fs);
        g_app.needs_redraw = 1;
        break;
    case KEY_ESC:
        if (panel->in_drive_list) {
            panel_exit_drives(panel, g_app.fs);
        } else {
            panel_clear_tags(panel);
        }
        g_app.needs_redraw = 1;
        break;
    case KEY_SPACE:
        panel_toggle_tag(panel);
        g_app.needs_redraw = 1;
        break;
    case KEY_F2:
        panel_refresh(panel, g_app.fs);
        g_app.needs_redraw = 1;
        break;
    case KEY_F5: {
        if (bgop_is_active(&g_app.bgtask)) break;
        Panel *other = (panel_idx == 0) ? &g_app.panels[1] : &g_app.panels[0];
        FileEntry *entries;
        int count;
        if (panel_tagged_or_current(panel, &entries, &count)) {
            BgTask *bg = &g_app.bgtask;
            bgop_lock(bg);
            bg->path_count = 0;
            for (int i = 0; i < count && i < 4096; i++)
                bg->paths[bg->path_count++] = path_join(panel_current_path(panel), entries[i].name);
            wcscpy_s(bg->dest_dir, BGOP_PATH_MAX, panel_current_path(other));
            bg->panel_src = panel; bg->panel_dst = other;
            bg->fs_provider = (void *)g_app.fs;
            bgop_unlock(bg);
            free(entries);
            panel_clear_tags(panel);
            bgop_start_copy(bg);
        }
        g_app.needs_redraw = 1;
        break;
    }
    case KEY_F6: {
        if (bgop_is_active(&g_app.bgtask)) break;
        Panel *other = (panel_idx == 0) ? &g_app.panels[1] : &g_app.panels[0];
        FileEntry *entries;
        int count;
        if (panel_tagged_or_current(panel, &entries, &count)) {
            BgTask *bg = &g_app.bgtask;
            bgop_lock(bg);
            bg->path_count = 0;
            for (int i = 0; i < count && i < 4096; i++)
                bg->paths[bg->path_count++] = path_join(panel_current_path(panel), entries[i].name);
            wcscpy_s(bg->dest_dir, BGOP_PATH_MAX, panel_current_path(other));
            bg->panel_src = panel; bg->panel_dst = other;
            bg->fs_provider = (void *)g_app.fs;
            bgop_unlock(bg);
            free(entries);
            panel_clear_tags(panel);
            bgop_start_move(bg);
        }
        g_app.needs_redraw = 1;
        break;
    }
    case KEY_F7: {
        wchar_t new_name[256] = {0};
        int created = ops_mkdir_dialog(panel_current_path(panel), &g_app.theme, g_app.fs, new_name, 256);
        panel_refresh(panel, g_app.fs);
        g_app.needs_redraw = 1;
        (void)created;
        break;
    }
    case KEY_F8: {
        if (bgop_is_active(&g_app.bgtask)) break;
        FileEntry *entries;
        int count;
        if (panel_tagged_or_current(panel, &entries, &count)) {
            wchar_t msg[512];
            swprintf_s(msg, 512, L"Delete %d item(s)?", count);
            if (ui_confirm_dialog(&g_app.theme, L"Delete", msg)) {
                BgTask *bg = &g_app.bgtask;
                bgop_lock(bg);
                bg->path_count = 0;
                for (int i = 0; i < count && i < 4096; i++)
                    bg->paths[bg->path_count++] = path_join(panel_current_path(panel), entries[i].name);
                bg->dest_dir[0] = 0;
                bg->panel_src = panel; bg->panel_dst = NULL;
                bg->fs_provider = (void *)g_app.fs;
                bgop_unlock(bg);
                panel_clear_tags(panel);
                bgop_start_delete(bg);
            }
            free(entries);
        }
        g_app.needs_redraw = 1;
        break;
    }
    case KEY_F10:
        break;
    case KEY_F12:
        g_app.running = 0;
        break;
    case KEY_TAB:
        g_app.focus = (panel_idx == 0) ? FOCUS_RIGHT : FOCUS_CMDLINE;
        g_app.needs_redraw = 1;
        break;
    case KEY_CTRL_C:
        panel_clear_tags(panel);
        g_app.needs_redraw = 1;
        break;
    default:
        break;
    }
}

static void handle_cmdline_input(KeyEvent *ev) {
    if (ev->code == KEY_RESIZE) {
        g_app.needs_redraw = 1;
        return;
    }

    /* if output is showing, any key dismisses it */
    if (g_app.cmdline.show_output) {
        g_app.cmdline.show_output = 0;
        free(g_app.cmdline.last_output);
        g_app.cmdline.last_output = NULL;
        g_app.needs_redraw = 1;
        return;
    }

    if (bgop_is_active(&g_app.bgtask) && ev->code == KEY_ESC) {
        g_app.bgtask.visible = 0;
        g_app.needs_redraw = 1;
        return;
    }
    if (bgop_is_active(&g_app.bgtask) && ev->code == KEY_F3) {
        g_app.bgtask.visible = !g_app.bgtask.visible;
        g_app.needs_redraw = 1;
        return;
    }

    switch (ev->code) {
    case KEY_F1:
        g_app.show_help = !g_app.show_help;
        g_app.needs_redraw = 1;
        break;
    case KEY_ENTER:
        cmdline_execute(&g_app.cmdline);
        g_app.needs_redraw = 1;
        break;
    case KEY_ESC:
        cmdline_clear(&g_app.cmdline);
        g_app.needs_redraw = 1;
        break;
    case KEY_TAB:
        g_app.focus = FOCUS_LEFT;
        g_app.needs_redraw = 1;
        break;
    case KEY_F10:
        break;
    case KEY_F12:
        g_app.running = 0;
        break;
    case KEY_BACKSPACE:
        cmdline_backspace(&g_app.cmdline);
        g_app.needs_redraw = 1;
        break;
    case KEY_DELETE:
        cmdline_delete(&g_app.cmdline);
        g_app.needs_redraw = 1;
        break;
    case KEY_LEFT:
        cmdline_cursor_left(&g_app.cmdline);
        g_app.needs_redraw = 1;
        break;
    case KEY_RIGHT:
        cmdline_cursor_right(&g_app.cmdline);
        g_app.needs_redraw = 1;
        break;
    case KEY_HOME:
        cmdline_cursor_home(&g_app.cmdline);
        g_app.needs_redraw = 1;
        break;
    case KEY_END:
        cmdline_cursor_end(&g_app.cmdline);
        g_app.needs_redraw = 1;
        break;
    case KEY_UP:
        cmdline_history_prev(&g_app.cmdline);
        g_app.needs_redraw = 1;
        break;
    case KEY_DOWN:
        cmdline_history_next(&g_app.cmdline);
        g_app.needs_redraw = 1;
        break;
    case KEY_CHAR:
        if (ev->ch && ev->ch >= 32) {
            cmdline_insert(&g_app.cmdline, ev->ch);
            g_app.needs_redraw = 1;
        }
        break;
    case KEY_SPACE:
        cmdline_insert(&g_app.cmdline, L' ');
        g_app.needs_redraw = 1;
        break;
    default:
        break;
    }
}

static int panel_h_for(int idx) {
    (void)idx;
    int panel_h = g_app.th - 2;
    if (panel_h < 6) panel_h = 6;
    return panel_h - 5;
}

int main(void) {
    init_app();

    while (g_app.running) {
        int old_w = g_app.tw, old_h = g_app.th;
        ui_get_term_size(&g_app.tw, &g_app.th);
        if (g_app.tw != old_w || g_app.th != old_h) {
            g_app.left_w = (g_app.tw * g_app.config.panel_split_pct) / 100;
            if (g_app.left_w < 20) g_app.left_w = 20;
            if (g_app.left_w > g_app.tw - 20) g_app.left_w = g_app.tw - 20;
            g_app.needs_redraw = 1;
        }

        /* check for background task completion */
        if (bgop_is_active(&g_app.bgtask) &&
            InterlockedCompareExchange(&g_app.bgtask.finished, 0, 0)) {
            Panel *src = (Panel *)g_app.bgtask.panel_src;
            Panel *dst = (Panel *)g_app.bgtask.panel_dst;
            panel_refresh(src, g_app.fs);
            if (dst) panel_refresh(dst, g_app.fs);
            bgop_free(&g_app.bgtask);
            bgop_init(&g_app.bgtask);
            g_app.needs_redraw = 1;
        }

        /* force redraw while bgop is active (progress bar animation) */
        if (bgop_is_active(&g_app.bgtask)) {
            g_app.needs_redraw = 1;
        }

        if (g_app.needs_redraw) {
            render();
            g_app.needs_redraw = 0;
        }

        KeyEvent ev;
        int got_input;
        if (bgop_is_active(&g_app.bgtask)) {
            /* poll with timeout so progress bar updates */
            got_input = input_poll_timeout(&ev, 100);
        } else {
            got_input = input_poll(&ev);
        }

        if (got_input) {
            switch (ev.code) {
            case KEY_F1:
                g_app.show_help = !g_app.show_help;
                g_app.needs_redraw = 1;
                break;
            case KEY_F12:
                g_app.running = 0;
                break;
            case KEY_RESIZE:
                g_app.needs_redraw = 1;
                break;
            default:
                if (g_app.focus == FOCUS_LEFT) {
                    handle_panel_input(&g_app.panels[0], 0, &ev);
                } else if (g_app.focus == FOCUS_RIGHT) {
                    handle_panel_input(&g_app.panels[1], 1, &ev);
                } else {
                    handle_cmdline_input(&ev);
                }
                break;
            }
        }
    }

    shutdown_app();
    return 0;
}
