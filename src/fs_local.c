#include "fs.h"
#include "utils.h"
#include <stdlib.h>
#include <stdio.h>

static int fs_local_list_dir(const wchar_t *path, FileEntry **entries, int *count) {
    *entries = NULL;
    *count = 0;

    wchar_t search_path[520];
    swprintf_s(search_path, 520, L"%s\\*", path);

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(search_path, &fd);
    if (h == INVALID_HANDLE_VALUE) return -1;

    int cap = 256;
    int cnt = 0;
    FileEntry *list = (FileEntry *)xcalloc(cap, sizeof(FileEntry));

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;

        if (cnt >= cap) {
            cap *= 2;
            list = (FileEntry *)xrealloc(list, cap * sizeof(FileEntry));
        }

        wcscpy_s(list[cnt].name, 256, fd.cFileName);
        list[cnt].attrs = fd.dwFileAttributes;
        list[cnt].size = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        list[cnt].modified = fd.ftLastWriteTime;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            list[cnt].type = ENTRY_DIR;
        } else if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            list[cnt].type = ENTRY_SYMLINK;
        } else {
            list[cnt].type = ENTRY_FILE;
        }
        cnt++;
    } while (FindNextFileW(h, &fd));

    FindClose(h);

    *entries = list;
    *count = cnt;
    return 0;
}

static int fs_local_stat(const wchar_t *path, FileEntry *out) {
    WIN32_FILE_ATTRIBUTE_DATA attr;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &attr)) return -1;
    memset(out, 0, sizeof(FileEntry));
    const wchar_t *name = wcsrchr(path, L'\\');
    if (!name) name = path; else name++;
    wcscpy_s(out->name, 256, name);
    out->attrs = attr.dwFileAttributes;
    out->size = ((uint64_t)attr.nFileSizeHigh << 32) | attr.nFileSizeLow;
    out->modified = attr.ftLastWriteTime;
    if (attr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        out->type = ENTRY_DIR;
    else if (attr.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        out->type = ENTRY_SYMLINK;
    else
        out->type = ENTRY_FILE;
    return 0;
}

static int fs_local_exists(const wchar_t *path) {
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static int fs_local_is_dir(const wchar_t *path) {
    DWORD attrs = GetFileAttributesW(path);
    return (attrs != INVALID_FILE_ATTRIBUTES) && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static void fs_local_free(FileEntry *entries) {
    free(entries);
}

static int fs_local_mkdir(const wchar_t *path) {
    return CreateDirectoryW(path, NULL) ? 0 : -1;
}

FsProvider fs_local = {
    fs_local_list_dir,
    fs_local_stat,
    fs_local_exists,
    fs_local_is_dir,
    fs_local_free,
    fs_local_mkdir
};
