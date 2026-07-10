#ifndef MAIN_H
#define MAIN_H

#include <wchar.h>
#include "panel.h"
#include "cmdline.h"
#include "theme.h"
#include "config.h"
#include "input.h"
#include "fs.h"

typedef enum {
    FOCUS_LEFT,
    FOCUS_RIGHT,
    FOCUS_CMDLINE
} FocusTarget;

typedef struct {
    Panel       panels[2];
    CmdLine     cmdline;
    FocusTarget focus;
    Theme       theme;
    Config      config;
    int         running;
    int         tw, th;
    int         left_w;
    int         needs_redraw;
    const FsProvider *fs;
} AppState;

extern AppState g_app;

#endif
