#include "fs.h"
#include "utils.h"
#include <stdlib.h>
#include <stdio.h>

int fs_entries_sort(FileEntry *entries, int count, int sort_by, int reverse) {
    if (count <= 1) return 0;
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - 1 - i; j++) {
            FileEntry *a = &entries[j];
            FileEntry *b = &entries[j + 1];
            int swap = 0;

            /* dirs always first */
            int a_isdir = (a->type == ENTRY_DIR);
            int b_isdir = (b->type == ENTRY_DIR);
            if (a_isdir && !b_isdir) { swap = 0; }
            else if (!a_isdir && b_isdir) { swap = 1; }
            else {
                int cmp = 0;
                switch (sort_by) {
                case 0: /* name */
                    cmp = wcs_compare_natural(a->name, b->name);
                    break;
                case 1: /* size */
                    if (a->size < b->size) cmp = -1;
                    else if (a->size > b->size) cmp = 1;
                    if (cmp == 0) cmp = wcs_compare_natural(a->name, b->name);
                    break;
                case 2: /* date */
                    cmp = CompareFileTime(&a->modified, &b->modified);
                    if (cmp == 0) cmp = wcs_compare_natural(a->name, b->name);
                    break;
                }
                if (reverse) cmp = -cmp;
                if (cmp > 0) swap = 1;
            }
            if (swap) {
                FileEntry tmp = entries[j];
                entries[j] = entries[j + 1];
                entries[j + 1] = tmp;
            }
        }
    }
    return 0;
}
