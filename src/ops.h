#ifndef OPS_H
#define OPS_H

#include <wchar.h>
#include "theme.h"
#include "fs.h"

typedef enum {
    OP_COPY,
    OP_MOVE,
    OP_DELETE
} OpType;

int ops_copy_files(const wchar_t **paths, int count, const wchar_t *dest_dir,
                   const Theme *theme);
int ops_move_files(const wchar_t **paths, int count, const wchar_t *dest_dir,
                   const Theme *theme);
int ops_delete_files(const wchar_t **paths, int count, const Theme *theme);
int ops_mkdir_dialog(const wchar_t *parent_dir, const Theme *theme,
                     const FsProvider *fs, wchar_t *new_name_out, int name_sz);

#endif
