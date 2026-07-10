#ifndef FS_H
#define FS_H

#include <wchar.h>
#include <windows.h>
#include <stdint.h>

typedef enum {
    ENTRY_FILE,
    ENTRY_DIR,
    ENTRY_SYMLINK
} EntryType;

typedef struct {
    wchar_t   name[256];
    EntryType type;
    uint64_t  size;
    FILETIME  modified;
    DWORD     attrs;
} FileEntry;

typedef struct FsProvider {
    int  (*list_dir)(const wchar_t *path, FileEntry **entries, int *count);
    int  (*stat_path)(const wchar_t *path, FileEntry *out);
    int  (*exists)(const wchar_t *path);
    int  (*is_dir)(const wchar_t *path);
    void (*free_entries)(FileEntry *entries);
    int  (*mkdir)(const wchar_t *path);
} FsProvider;

extern FsProvider fs_local;

int fs_entries_sort(FileEntry *entries, int count, int sort_by, int reverse);

#endif
