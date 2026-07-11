#include "bgop.h"
#include "utils.h"
#include "fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <process.h>

void bgop_init(BgTask *t) {
    memset(t, 0, sizeof(BgTask));
    InitializeCriticalSection(&t->cs);
}

void bgop_free(BgTask *t) {
    bgop_lock(t);
    if (t->thread) {
        CloseHandle(t->thread);
        t->thread = NULL;
    }
    for (int i = 0; i < t->path_count; i++) free(t->paths[i]);
    t->path_count = 0;
    bgop_unlock(t);
    DeleteCriticalSection(&t->cs);
    memset(t, 0, sizeof(BgTask));
}

int bgop_is_active(const BgTask *t) {
    return InterlockedCompareExchange((LONG volatile *)&t->active, 0, 0) != 0;
}

void bgop_lock(BgTask *t)  { EnterCriticalSection(&t->cs); }
void bgop_unlock(BgTask *t){ LeaveCriticalSection(&t->cs); }

void bgop_history_push(BgOpRecord *hist, int *count, int cap,
                       BgOpType op, int total, int done,
                       const wchar_t *desc, int status) {
    if (*count >= cap) {
        memmove(hist, hist + 1, (cap - 1) * sizeof(BgOpRecord));
        *count = cap - 1;
    }
    BgOpRecord *r = &hist[*count];
    r->op_type = op;
    r->total   = total;
    r->done    = done;
    r->status  = status;
    if (desc)
        wcsncpy_s(r->desc, 128, desc, 127);
    else
        r->desc[0] = 0;
    (*count)++;
}

static int bg_copy_single_file(const wchar_t *src, const wchar_t *dest) {
    if (GetFileAttributesW(dest) != INVALID_FILE_ATTRIBUTES) {
        SetFileAttributesW(dest, FILE_ATTRIBUTE_NORMAL);
    }
    return CopyFileW(src, dest, FALSE);
}

static int bg_copy_dir_tree(const wchar_t *src, const wchar_t *dest) {
    if (!CreateDirectoryW(dest, NULL)) {
        if (GetLastError() != ERROR_ALREADY_EXISTS) return -1;
    }
    wchar_t search[520];
    swprintf_s(search, 520, L"%s\\*", src);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(search, &fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    int ok = 0;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;
        wchar_t *sf = path_join(src, fd.cFileName);
        wchar_t *df = path_join(dest, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            bg_copy_dir_tree(sf, df);
        } else {
            bg_copy_single_file(sf, df);
        }
        free(sf); free(df);
        ok = 1;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return ok;
}

static int bg_delete_dir_tree(const wchar_t *path) {
    wchar_t search[520];
    swprintf_s(search, 520, L"%s\\*", path);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(search, &fd);
    if (h == INVALID_HANDLE_VALUE) { RemoveDirectoryW(path); return 0; }
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;
        wchar_t *full = path_join(path, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            bg_delete_dir_tree(full);
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

static void update_progress(BgTask *t, const wchar_t *file) {
    bgop_lock(t);
    if (file) wcscpy_s(t->current_file, 260, file);
    InterlockedIncrement(&t->done_items);
    bgop_unlock(t);
}

static unsigned int __stdcall bg_thread_copy(void *param) {
    BgTask *t = (BgTask *)param;
    t->start_ticks = GetTickCount();
    wcscpy_s(t->title, 64, L"Copying files...");

    int any_ssh = ssh_is_ssh_path(t->paths[0]) || ssh_is_ssh_path(t->dest_dir);

    for (int i = 0; i < t->path_count && !t->error; i++) {
        const wchar_t *src  = t->paths[i];
        const wchar_t *name = wcsrchr(src, L'\\');
        if (!name) name = src; else name++;

        wchar_t *dest = path_join(t->dest_dir, name);
        if (!dest) continue;

        int ok = 0;
        if (any_ssh) {
            ok = (ssh_transfer_file(src, dest) == 0);
        } else {
            DWORD attrs = GetFileAttributesW(src);
            if (attrs == INVALID_FILE_ATTRIBUTES) { free(dest); continue; }
            if (attrs & FILE_ATTRIBUTE_DIRECTORY)
                ok = (bg_copy_dir_tree(src, dest) >= 0);
            else
                ok = (bg_copy_single_file(src, dest) != 0);
        }

        update_progress(t, name);
        free(dest);
        (void)ok;
    }

    InterlockedExchange(&t->finished, 1);
    InterlockedExchange(&t->active, 0);
    return 0;
}

static unsigned int __stdcall bg_thread_move(void *param) {
    BgTask *t = (BgTask *)param;
    t->start_ticks = GetTickCount();
    wcscpy_s(t->title, 64, L"Moving files...");

    int any_ssh = ssh_is_ssh_path(t->paths[0]) || ssh_is_ssh_path(t->dest_dir);

    for (int i = 0; i < t->path_count && !t->error; i++) {
        const wchar_t *src  = t->paths[i];
        const wchar_t *name = wcsrchr(src, L'\\');
        if (!name) name = src; else name++;

        wchar_t *dest = path_join(t->dest_dir, name);
        if (!dest) continue;

        if (any_ssh) {
            /* copy then delete source */
            if (ssh_transfer_file(src, dest) == 0) {
                if (ssh_is_ssh_path(src))
                    ssh_delete_path(src);
                else if (GetFileAttributesW(src) & FILE_ATTRIBUTE_DIRECTORY)
                    bg_delete_dir_tree(src);
                else
                    DeleteFileW(src);
            }
        } else {
            DWORD attrs = GetFileAttributesW(src);
            if (attrs == INVALID_FILE_ATTRIBUTES) { free(dest); continue; }
            if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
                if (!MoveFileExW(src, dest, MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING)) {
                    bg_copy_dir_tree(src, dest);
                    bg_delete_dir_tree(src);
                }
            } else {
                if (GetFileAttributesW(dest) != INVALID_FILE_ATTRIBUTES)
                    SetFileAttributesW(dest, FILE_ATTRIBUTE_NORMAL);
                MoveFileExW(src, dest, MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING);
            }
        }

        update_progress(t, name);
        free(dest);
    }

    InterlockedExchange(&t->finished, 1);
    InterlockedExchange(&t->active, 0);
    return 0;
}

static unsigned int __stdcall bg_thread_delete(void *param) {
    BgTask *t = (BgTask *)param;
    t->start_ticks = GetTickCount();
    wcscpy_s(t->title, 64, L"Deleting files...");

    int any_ssh = ssh_is_ssh_path(t->paths[0]);

    for (int i = 0; i < t->path_count && !t->error; i++) {
        const wchar_t *src = t->paths[i];
        const wchar_t *name = wcsrchr(src, L'\\');
        if (!name) name = src; else name++;

        if (any_ssh) {
            ssh_delete_path(src);
        } else {
            DWORD attrs = GetFileAttributesW(src);
            if (attrs == INVALID_FILE_ATTRIBUTES) continue;
            if (attrs & FILE_ATTRIBUTE_DIRECTORY)
                bg_delete_dir_tree(src);
            else {
                SetFileAttributesW(src, FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(src);
            }
        }
        update_progress(t, name);
    }

    InterlockedExchange(&t->finished, 1);
    InterlockedExchange(&t->active, 0);
    return 0;
}

int bgop_start_copy(BgTask *t) {
    if (bgop_is_active(t)) return 0;
    t->op_type = BGOP_COPY;
    t->active  = 1;
    t->finished = 0;
    t->visible  = 1;
    t->error    = 0;
    t->done_items = 0;
    t->total_items = t->path_count;
    t->current_file[0] = 0;
    t->thread = (HANDLE)_beginthreadex(NULL, 0, bg_thread_copy, t, 0, NULL);
    return t->thread != NULL;
}

int bgop_start_move(BgTask *t) {
    if (bgop_is_active(t)) return 0;
    t->op_type = BGOP_MOVE;
    t->active  = 1;
    t->finished = 0;
    t->visible  = 1;
    t->error    = 0;
    t->done_items = 0;
    t->total_items = t->path_count;
    t->current_file[0] = 0;
    t->thread = (HANDLE)_beginthreadex(NULL, 0, bg_thread_move, t, 0, NULL);
    return t->thread != NULL;
}

int bgop_start_delete(BgTask *t) {
    if (bgop_is_active(t)) return 0;
    t->op_type = BGOP_DELETE;
    t->active  = 1;
    t->finished = 0;
    t->visible  = 1;
    t->error    = 0;
    t->done_items = 0;
    t->total_items = t->path_count;
    t->current_file[0] = 0;
    t->thread = (HANDLE)_beginthreadex(NULL, 0, bg_thread_delete, t, 0, NULL);
    return t->thread != NULL;
}
