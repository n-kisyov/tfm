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

static int copy_dir_recursive(const wchar_t *src, const wchar_t *dest,
                              const Theme *theme, int *copied_count);

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

static int copy_dir_recursive(const wchar_t *src, const wchar_t *dest,
                              const Theme *theme, int *copied_count) {
    if (!CreateDirectoryW(dest, NULL)) {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS) return -1;
    }

    wchar_t search[520];
    swprintf_s(search, 520, L"%s\\*", src);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(search, &fd);
    if (h == INVALID_HANDLE_VALUE) return -1;

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;

        wchar_t *src_full  = path_join(src, fd.cFileName);
        wchar_t *dest_full = path_join(dest, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            copy_dir_recursive(src_full, dest_full, theme, copied_count);
        } else {
            if (GetFileAttributesW(dest_full) != INVALID_FILE_ATTRIBUTES) {
                if (!confirm_overwrite(theme, fd.cFileName)) {
                    free(src_full); free(dest_full);
                    continue;
                }
                SetFileAttributesW(dest_full, FILE_ATTRIBUTE_NORMAL);
            }
            if (CopyFileW(src_full, dest_full, FALSE))
                (*copied_count)++;
        }

        free(src_full);
        free(dest_full);
    } while (FindNextFileW(h, &fd));

    FindClose(h);
    return 0;
}

int ops_copy_files(const wchar_t **paths, int count, const wchar_t *dest_dir,
                   const Theme *theme) {
    if (!paths || count <= 0 || !dest_dir) return -1;
    if (wcsncmp(dest_dir, L"\\\\ssh\\", 6) == 0 && count > 0 &&
        wcsncmp(paths[0], L"\\\\ssh\\", 6) == 0) {
        ui_message_box(theme, L"Error", L"Remote-to-remote copy not supported");
        return 0;
    }
    int copied = 0;
    for (int i = 0; i < count; i++) {
        const wchar_t *src = paths[i];
        const wchar_t *name = wcsrchr(src, L'\\');
        if (!name) name = src; else name++;
        wchar_t *dest = path_join(dest_dir, name);
        if (!dest) continue;

        DWORD attrs = GetFileAttributesW(src);
        if (attrs == INVALID_FILE_ATTRIBUTES) { free(dest); continue; }

        if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
            copy_dir_recursive(src, dest, theme, &copied);
        } else {
            if (GetFileAttributesW(dest) != INVALID_FILE_ATTRIBUTES) {
                if (!confirm_overwrite(theme, name)) { free(dest); continue; }
                SetFileAttributesW(dest, FILE_ATTRIBUTE_NORMAL);
            }
            if (CopyFileW(src, dest, FALSE)) copied++;
        }
        free(dest);
    }
    return copied;
}

static int move_dir_recursive(const wchar_t *src, const wchar_t *dest,
                              const Theme *theme, int *moved_count) {
    /* try fast path first — MoveFileEx works cross-volume */
    if (MoveFileExW(src, dest, MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING)) {
        (*moved_count)++;
        return 0;
    }

    /* fallback: copy tree then delete */
    int copied = 0;
    if (copy_dir_recursive(src, dest, theme, &copied) == 0 && copied > 0) {
        delete_dir_recursive(src);
        (*moved_count)++;
    }
    return 0;
}

int ops_move_files(const wchar_t **paths, int count, const wchar_t *dest_dir,
                   const Theme *theme) {
    if (!paths || count <= 0 || !dest_dir) return -1;
    if (wcsncmp(dest_dir, L"\\\\ssh\\", 6) == 0 && count > 0 &&
        wcsncmp(paths[0], L"\\\\ssh\\", 6) == 0) {
        ui_message_box(theme, L"Error", L"Remote-to-remote move not supported");
        return 0;
    }
    int moved = 0;
    for (int i = 0; i < count; i++) {
        const wchar_t *src = paths[i];
        const wchar_t *name = wcsrchr(src, L'\\');
        if (!name) name = src; else name++;
        wchar_t *dest = path_join(dest_dir, name);
        if (!dest) continue;

        DWORD attrs = GetFileAttributesW(src);
        if (attrs == INVALID_FILE_ATTRIBUTES) { free(dest); continue; }

        if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
            move_dir_recursive(src, dest, theme, &moved);
        } else {
            if (GetFileAttributesW(dest) != INVALID_FILE_ATTRIBUTES) {
                if (!confirm_overwrite(theme, name)) { free(dest); continue; }
                SetFileAttributesW(dest, FILE_ATTRIBUTE_NORMAL);
            }
            if (MoveFileExW(src, dest, MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING))
                moved++;
        }
        free(dest);
    }
    return moved;
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
    int ok = fs->mkdir(full);  /* 0 = success, -1 = failure */
    if (new_name_out && name_sz > 0) wcscpy_s(new_name_out, name_sz, name);
    free(full);
    if (ok != 0) {
        wchar_t msg[512];
        swprintf_s(msg, 512, L"Failed to create \"%s\"", name);
        ui_message_box(theme, L"Error", msg);
        return 0;
    }
    return 1;
}
