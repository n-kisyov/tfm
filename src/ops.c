#include "ops.h"
#include "ui.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>

static int confirm_overwrite(const Theme *theme, const wchar_t *name) {
    wchar_t msg[1024];
    swprintf_s(msg, 1024, L"Overwrite \"%s\"?", name);
    return ui_confirm_dialog(theme, L"File exists", msg);
}

int ops_copy_files(const wchar_t **paths, int count, const wchar_t *dest_dir,
                   const Theme *theme) {
    if (!paths || count <= 0 || !dest_dir) return -1;
    int copied = 0;
    for (int i = 0; i < count; i++) {
        const wchar_t *src = paths[i];
        const wchar_t *name = wcsrchr(src, L'\\');
        if (!name) name = src; else name++;
        wchar_t *dest = path_join(dest_dir, name);
        if (!dest) continue;
        if (GetFileAttributesW(dest) != INVALID_FILE_ATTRIBUTES) {
            if (!confirm_overwrite(theme, name)) { free(dest); continue; }
        }
        if (CopyFileW(src, dest, FALSE)) {
            copied++;
        }
        free(dest);
    }
    return copied;
}

int ops_move_files(const wchar_t **paths, int count, const wchar_t *dest_dir,
                   const Theme *theme) {
    if (!paths || count <= 0 || !dest_dir) return -1;
    int moved = 0;
    for (int i = 0; i < count; i++) {
        const wchar_t *src = paths[i];
        const wchar_t *name = wcsrchr(src, L'\\');
        if (!name) name = src; else name++;
        wchar_t *dest = path_join(dest_dir, name);
        if (!dest) continue;
        if (GetFileAttributesW(dest) != INVALID_FILE_ATTRIBUTES) {
            if (!confirm_overwrite(theme, name)) { free(dest); continue; }
        }
        if (MoveFileW(src, dest)) {
            moved++;
        }
        free(dest);
    }
    return moved;
}

static int delete_dir_recursive(const wchar_t *path) {
    wchar_t search[520];
    swprintf_s(search, 520, L"%s\\*", path);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(search, &fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;
        wchar_t *full = path_join(path, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            delete_dir_recursive(full);
        } else {
            SetFileAttributesW(full, FILE_ATTRIBUTE_NORMAL);
            DeleteFileW(full);
        }
        free(full);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL);
    RemoveDirectoryW(path);
    return 0;
}

int ops_delete_files(const wchar_t **paths, int count, const Theme *theme) {
    (void)theme;
    if (!paths || count <= 0) return -1;
    int deleted = 0;
    for (int i = 0; i < count; i++) {
        DWORD attrs = GetFileAttributesW(paths[i]);
        if (attrs == INVALID_FILE_ATTRIBUTES) continue;
        if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
            delete_dir_recursive(paths[i]);
            deleted++;
        } else {
            SetFileAttributesW(paths[i], FILE_ATTRIBUTE_NORMAL);
            if (DeleteFileW(paths[i])) deleted++;
        }
    }
    return deleted;
}

int ops_mkdir_dialog(const wchar_t *parent_dir, const Theme *theme,
                     const FsProvider *fs, wchar_t *new_name_out, int name_sz) {
    wchar_t name[256] = {0};
    if (!ui_input_dialog(theme, L"Create directory name:", name, 256)) return 0;
    if (name[0] == 0) return 0;
    wchar_t *full = path_join(parent_dir, name);
    int ok = fs->mkdir(full);
    if (new_name_out && name_sz > 0) wcscpy_s(new_name_out, name_sz, name);
    free(full);
    if (!ok) {
        wchar_t msg[512];
        swprintf_s(msg, 512, L"Failed to create \"%s\"", name);
        ui_message_box(theme, L"Error", msg);
        return 0;
    }
    return 1;
}
