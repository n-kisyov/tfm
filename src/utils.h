#ifndef UTILS_H
#define UTILS_H

#include <wchar.h>
#include <windows.h>
#include <stdint.h>

#define MAX_PATH_LEN 520
#define MAX_CMDLINE_LEN 4096
#define MAX_HISTORY 100
#define MAX_TABS 8
#define MAX_ENTRIES_PER_DIR 4096
#define MAX_ENTRY_STRING 256
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

wchar_t *wcsdup_safe(const wchar_t *src);
void    *xcalloc(size_t n, size_t sz);
void    *xrealloc(void *p, size_t sz);
int      wcs_ends_with(const wchar_t *str, const wchar_t *suffix);
wchar_t *path_join(const wchar_t *dir, const wchar_t *name);
wchar_t *get_parent_path(const wchar_t *path);
int      is_root_path(const wchar_t *path);
wchar_t *get_file_ext(const wchar_t *name);
int      wcs_compare_natural(const wchar_t *a, const wchar_t *b);
wchar_t *get_home_dir(void);
wchar_t *get_config_dir(void);
wchar_t *format_file_size(uint64_t bytes, wchar_t *buf, size_t buf_size);
wchar_t *format_file_time(const FILETIME *ft, wchar_t *buf, size_t buf_size);

#endif
