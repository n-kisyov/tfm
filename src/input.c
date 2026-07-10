#include "input.h"
#include <stdlib.h>
#include <stdio.h>

static HANDLE g_hStdin = NULL;
static DWORD g_oldMode = 0;
static int   g_initialized = 0;
static int   g_lastW = 0, g_lastH = 0;

int input_init(void) {
    g_hStdin = GetStdHandle(STD_INPUT_HANDLE);
    if (g_hStdin == INVALID_HANDLE_VALUE) return -1;

    if (!GetConsoleMode(g_hStdin, &g_oldMode)) return -1;

    DWORD mode = ENABLE_WINDOW_INPUT;
    if (!SetConsoleMode(g_hStdin, mode)) return -1;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        g_lastW = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        g_lastH = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }

    g_initialized = 1;
    return 0;
}

void input_shutdown(void) {
    if (g_initialized && g_hStdin) {
        SetConsoleMode(g_hStdin, g_oldMode);
    }
    g_initialized = 0;
}

static KeyCode map_vk(WORD vk, DWORD ctrl) {
    int shift = (ctrl & SHIFT_PRESSED) != 0;
    int ctrl_pressed = (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
    int alt = (ctrl & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;

    switch (vk) {
    case VK_TAB:
        if (ctrl_pressed && shift) return KEY_CTRL_SHIFT_TAB;
        if (ctrl_pressed) return KEY_CTRL_TAB;
        return KEY_TAB;
    case VK_RETURN:   return KEY_ENTER;
    case VK_ESCAPE:   return KEY_ESC;
    case VK_UP:       return KEY_UP;
    case VK_DOWN:     return KEY_DOWN;
    case VK_LEFT:     return KEY_LEFT;
    case VK_RIGHT:    return KEY_RIGHT;
    case VK_PRIOR:    return KEY_PAGEUP;
    case VK_NEXT:     return KEY_PAGEDOWN;
    case VK_HOME:     return KEY_HOME;
    case VK_END:      return KEY_END;
    case VK_SPACE:    return KEY_SPACE;
    case VK_BACK:     return KEY_BACKSPACE;
    case VK_DELETE:   return KEY_DELETE;
    case VK_INSERT:   return KEY_INSERT;
    case VK_F1:  return KEY_F1;
    case VK_F2:  return KEY_F2;
    case VK_F3:  return KEY_F3;
    case VK_F4:  return KEY_F4;
    case VK_F5:  return KEY_F5;
    case VK_F6:  return KEY_F6;
    case VK_F7:  return KEY_F7;
    case VK_F8:  return KEY_F8;
    case VK_F9:  return KEY_F9;
    case VK_F10: return KEY_F10;
    case VK_F11: return KEY_F11;
    case VK_F12: return KEY_F12;
    default:
        if (ctrl_pressed && !alt) {
            if (vk == L'T') return KEY_CTRL_T;
            if (vk == L'W') return KEY_CTRL_W;
            if (vk == L'C') return KEY_CTRL_C;
            if (vk == L'R') return KEY_CTRL_R;
            if (vk == L'D') return KEY_CTRL_D;
        }
        return KEY_CHAR;
    }
}

int input_poll(KeyEvent *ev) {
    if (!g_initialized) return 0;
    memset(ev, 0, sizeof(KeyEvent));

    /* check for resize */
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        int w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        int h = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        if (w != g_lastW || h != g_lastH) {
            g_lastW = w;
            g_lastH = h;
            ev->code = KEY_RESIZE;
            return 1;
        }
    }

    INPUT_RECORD rec;
    DWORD read = 0;
    while (1) {
        if (!ReadConsoleInputW(g_hStdin, &rec, 1, &read)) return 0;
        if (read == 0) return 0;

        if (rec.EventType == WINDOW_BUFFER_SIZE_EVENT) {
            g_lastW = rec.Event.WindowBufferSizeEvent.dwSize.X;
            g_lastH = rec.Event.WindowBufferSizeEvent.dwSize.Y;
            ev->code = KEY_RESIZE;
            return 1;
        }

        if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown) {
            KEY_EVENT_RECORD *ke = &rec.Event.KeyEvent;
            KeyCode kc = map_vk(ke->wVirtualKeyCode, ke->dwControlKeyState);
            ev->code = kc;
            ev->ctrl_keys = ke->dwControlKeyState;
            if (kc == KEY_CHAR && ke->uChar.UnicodeChar) {
                ev->ch = ke->uChar.UnicodeChar;
            }
            if (kc != KEY_CHAR || ev->ch) {
                return 1;
            }
        }
    }
}

int input_poll_timeout(KeyEvent *ev, DWORD timeout_ms) {
    if (!g_initialized) return 0;
    memset(ev, 0, sizeof(KeyEvent));

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        int w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        int h = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        if (w != g_lastW || h != g_lastH) {
            g_lastW = w;
            g_lastH = h;
            ev->code = KEY_RESIZE;
            return 1;
        }
    }

    if (WaitForSingleObject(g_hStdin, timeout_ms) == WAIT_TIMEOUT)
        return 0;

    INPUT_RECORD rec;
    DWORD read = 0;
    /* non-blocking drain — read all pending events, return the first valid one */
    while (1) {
        DWORD pending = 0;
        if (!GetNumberOfConsoleInputEvents(g_hStdin, &pending) || pending == 0)
            return 0;
        if (!ReadConsoleInputW(g_hStdin, &rec, 1, &read)) return 0;
        if (read == 0) return 0;

        if (rec.EventType == WINDOW_BUFFER_SIZE_EVENT) {
            g_lastW = rec.Event.WindowBufferSizeEvent.dwSize.X;
            g_lastH = rec.Event.WindowBufferSizeEvent.dwSize.Y;
            ev->code = KEY_RESIZE;
            return 1;
        }
        if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown) {
            KEY_EVENT_RECORD *ke = &rec.Event.KeyEvent;
            KeyCode kc = map_vk(ke->wVirtualKeyCode, ke->dwControlKeyState);
            ev->code = kc;
            ev->ctrl_keys = ke->dwControlKeyState;
            if (kc == KEY_CHAR && ke->uChar.UnicodeChar)
                ev->ch = ke->uChar.UnicodeChar;
            if (kc != KEY_CHAR || ev->ch)
                return 1;
        }
    }
}
