#ifndef INPUT_H
#define INPUT_H

#include <windows.h>
#include <stdint.h>
#include <wchar.h>

typedef enum {
    KEY_NONE = 0,
    KEY_TAB,
    KEY_CTRL_TAB,
    KEY_CTRL_SHIFT_TAB,
    KEY_ENTER,
    KEY_ESC,
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_PAGEUP,
    KEY_PAGEDOWN,
    KEY_HOME,
    KEY_END,
    KEY_SPACE,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5,
    KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10,
    KEY_F11, KEY_F12,
    KEY_CTRL_T,
    KEY_CTRL_W,
    KEY_CTRL_C,
    KEY_CTRL_R,
    KEY_BACKSPACE,
    KEY_DELETE,
    KEY_INSERT,
    KEY_CHAR,
    KEY_RESIZE
} KeyCode;

typedef struct {
    KeyCode  code;
    wchar_t  ch;
    DWORD    ctrl_keys;
} KeyEvent;

int  input_init(void);
void input_shutdown(void);
int  input_poll(KeyEvent *ev);
int  input_poll_timeout(KeyEvent *ev, DWORD timeout_ms);

#endif
