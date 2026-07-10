#ifndef CONFIG_H
#define CONFIG_H

#include <wchar.h>
#include <stdint.h>
#include "utils.h"

#define CONFIG_MAX_PATH 520

typedef struct {
    wchar_t  theme_path[CONFIG_MAX_PATH];
    wchar_t  startup_dirs[2][MAX_TABS][CONFIG_MAX_PATH];
    int      startup_tab_counts[2];
    wchar_t  drive_paths[2][26][CONFIG_MAX_PATH];
    int      show_hidden;
    int      sort_by;       /* 0=name, 1=size, 2=date */
    int      sort_reverse;
    int      confirm_delete;
    int      panel_split_pct;  /* 0..100, default 50 */
} Config;

void config_set_defaults(Config *c);
int  config_load(Config *c, const wchar_t *path);
int  config_save(const Config *c, const wchar_t *path);
const wchar_t *config_get_path(void);

#endif
