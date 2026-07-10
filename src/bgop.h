#ifndef BGOP_H
#define BGOP_H

#include <windows.h>
#include <wchar.h>

#define BGOP_PATH_MAX  520
#define BGOP_HISTORY   30

typedef enum {
    BGOP_NONE = 0,
    BGOP_COPY,
    BGOP_MOVE,
    BGOP_DELETE
} BgOpType;

typedef struct {
    BgOpType op_type;
    int      total;
    int      done;
    wchar_t  desc[128];
    int      status;       /* 0=failed, 1=ok */
} BgOpRecord;

typedef struct BgTask {
    volatile LONG active;
    volatile LONG finished;
    volatile LONG visible;     /* user dismissed the progress bar */
    volatile LONG error;

    BgOpType  op_type;
    int       total_items;
    volatile LONG   done_items;
    wchar_t   current_file[260];
    wchar_t   title[64];
    DWORD     start_ticks;

    CRITICAL_SECTION cs;
    HANDLE    thread;

    wchar_t  *paths[4096];
    int       path_count;
    wchar_t   dest_dir[BGOP_PATH_MAX];

    /* panel pointers (set by caller, must stay valid) */
    int      panel_src_idx;
    int      panel_dst_idx;
    void     *fs_provider;
} BgTask;

void  bgop_init (BgTask *t);
void  bgop_free (BgTask *t);
int   bgop_is_active(const BgTask *t);
void  bgop_lock (BgTask *t);
void  bgop_unlock(BgTask *t);

int   bgop_start_copy  (BgTask *t);
int   bgop_start_move  (BgTask *t);
int   bgop_start_delete(BgTask *t);

void  bgop_history_push(BgOpRecord *history, int *count, int cap,
                        BgOpType op, int total, int done,
                        const wchar_t *desc, int status);

#endif
