#include "utils.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <shlobj.h>

wchar_t *wcsdup_safe(const wchar_t *src) {
    if (!src) return NULL;
    size_t len = wcslen(src) + 1;
    wchar_t *dst = (wchar_t *)malloc(len * sizeof(wchar_t));
    if (dst) wcscpy_s(dst, len, src);
    return dst;
}

void *xcalloc(size_t n, size_t sz) {
    void *p = calloc(n, sz);
    if (!p) abort();
    return p;
}

void *xrealloc(void *p, size_t sz) {
    void *np = realloc(p, sz);
    if (!np) abort();
    return np;
}

int wcs_ends_with(const wchar_t *str, const wchar_t *suffix) {
    size_t len_str = wcslen(str);
    size_t len_suf = wcslen(suffix);
    if (len_suf > len_str) return 0;
    return _wcsicmp(str + len_str - len_suf, suffix) == 0;
}

wchar_t *path_join(const wchar_t *dir, const wchar_t *name) {
    size_t dlen = wcslen(dir);
    size_t nlen = wcslen(name);
    int need_sep = (dlen > 0 && dir[dlen - 1] != L'\\' && dir[dlen - 1] != L'/');
    wchar_t *result = (wchar_t *)malloc((dlen + nlen + 2) * sizeof(wchar_t));
    if (!result) return NULL;
    wcscpy_s(result, dlen + 1, dir);
    if (need_sep) {
        result[dlen] = L'\\';
        dlen++;
    }
    wcscpy_s(result + dlen, nlen + 1, name);
    return result;
}

wchar_t *get_parent_path(const wchar_t *path) {
    size_t len = wcslen(path);
    if (len == 0) return wcsdup_safe(L"C:\\");
    /* Trim trailing backslashes except for root */
    while (len > 0 && (path[len - 1] == L'\\' || path[len - 1] == L'/')) {
        len--;
        if (len == 2 && path[1] == L':') break;
        if (len == 1 && path[0] == L'\\') break;
    }
    if (len == 2 && path[1] == L':') {
        wchar_t *r = (wchar_t *)malloc(4 * sizeof(wchar_t));
        wcscpy_s(r, 4, path);
        r[2] = L'\\';
        r[3] = 0;
        return r;
    }
    if (len == 0) return wcsdup_safe(L"C:\\");
    for (size_t i = len; i > 0; i--) {
        if (path[i - 1] == L'\\' || path[i - 1] == L'/') {
            if (i == 2 && path[1] == L':') {
                wchar_t *r = (wchar_t *)malloc(4 * sizeof(wchar_t));
                wcscpy_s(r, 4, path);
                r[2] = L'\\';
                r[3] = 0;
                return r;
            }
            wchar_t *r = (wchar_t *)malloc((i + 1) * sizeof(wchar_t));
            wcsncpy_s(r, i + 1, path, i);
            r[i] = 0;
            return r;
        }
    }
    return wcsdup_safe(L"C:\\");
}

int is_root_path(const wchar_t *path) {
    if (!path) return 0;
    size_t len = wcslen(path);
    if (len == 3 && path[1] == L':' && path[2] == L'\\') return 1;
    if (len == 2 && path[1] == L':' && path[2] == 0) return 1;
    if (len == 1 && path[0] == L'\\') return 1;
    return 0;
}

wchar_t *get_file_ext(const wchar_t *name) {
    const wchar_t *dot = wcsrchr(name, L'.');
    if (!dot) return L"";
    return (wchar_t *)(dot + 1);
}

static int wcs_natural_compare_part(const wchar_t **pa, const wchar_t **pb) {
    const wchar_t *a = *pa, *b = *pb;
    while (*a == L'0') a++;
    while (*b == L'0') b++;
    int diff = 0;
    while (1) {
        int da = (a[0] >= L'0' && a[0] <= L'9');
        int db = (b[0] >= L'0' && b[0] <= L'9');
        if (!da && !db) break;
        if (!da) { *pa = a; *pb = b; return -1; }
        if (!db) { *pa = a; *pb = b; return 1; }
        if (diff == 0) diff = (int)a[0] - (int)b[0];
        a++; b++;
    }
    *pa = a; *pb = b;
    return diff ? diff : (int)(a - *pa) - (int)(b - *pb);
}

int wcs_compare_natural(const wchar_t *a, const wchar_t *b) {
    while (*a && *b) {
        if ((*a >= L'0' && *a <= L'9') && (*b >= L'0' && *b <= L'9')) {
            int r = wcs_natural_compare_part(&a, &b);
            if (r != 0) return r;
        } else {
            int wa = towupper(*a);
            int wb = towupper(*b);
            if (wa != wb) return wa - wb;
            a++; b++;
        }
    }
    return (int)(*a) - (int)(*b);
}

wchar_t *get_home_dir(void) {
    static wchar_t buf[MAX_PATH_LEN] = {0};
    if (buf[0]) return buf;
    const wchar_t *drives[] = {L"HOMEDRIVE", L"USERPROFILE", L"HOME", NULL};
    const wchar_t *paths[]  = {L"HOMEPATH", NULL};
    wchar_t path_buf[MAX_PATH_LEN] = {0};
    for (int i = 0; drives[i]; i++) {
        if (GetEnvironmentVariableW(drives[i], buf, MAX_PATH_LEN) > 0) {
            if (paths[0] && GetEnvironmentVariableW(paths[0], path_buf, MAX_PATH_LEN) > 0) {
                swprintf_s(buf, MAX_PATH_LEN, L"%s%s", buf, path_buf);
            }
            return buf;
        }
    }
    if (GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH_LEN) == 0) {
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, buf))) {
            return buf;
        }
        wcscpy_s(buf, MAX_PATH_LEN, L"C:\\");
    }
    return buf;
}

wchar_t *get_config_dir(void) {
    static wchar_t buf[MAX_PATH_LEN] = {0};
    if (buf[0]) return buf;
    wchar_t *home = get_home_dir();
    swprintf_s(buf, MAX_PATH_LEN, L"%s\\.tfm", home);
    return buf;
}

wchar_t *format_file_size(uint64_t bytes, wchar_t *buf, size_t buf_size) {
    const wchar_t *units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    int unit_idx = 0;
    double size = (double)bytes;
    while (size >= 1024.0 && unit_idx < 4) {
        size /= 1024.0;
        unit_idx++;
    }
    if (unit_idx == 0) {
        swprintf_s(buf, buf_size, L"%llu %s", bytes, units[unit_idx]);
    } else {
        swprintf_s(buf, buf_size, L"%.1f %s", size, units[unit_idx]);
    }
    return buf;
}

wchar_t *format_file_time(const FILETIME *ft, wchar_t *buf, size_t buf_size) {
    SYSTEMTIME st_local, st;
    FileTimeToSystemTime(ft, &st);
    SystemTimeToTzSpecificLocalTime(NULL, &st, &st_local);
    GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st_local, NULL, buf, (int)buf_size);
    size_t dlen = wcslen(buf);
    buf[dlen] = L' ';
    dlen++;
    GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &st_local, NULL, buf + dlen, (int)(buf_size - dlen));
    return buf;
}
