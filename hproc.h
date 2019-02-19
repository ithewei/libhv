#ifndef H_PROC_H_
#define H_PROC_H_

#include "hdef.h"
#include "hplatform.h"
#include "hlog.h"
#include "hmain.h"

typedef struct proc_ctx_s {
    pid_t           pid; // tid in win32
    char            proctitle[256];
    procedure_t     proc;
    void*           userdata;
} proc_ctx_t;

#ifdef __unix__
// unix use multi-processes
inline int create_proc(proc_ctx_t* ctx) {
    pid_t pid = fork();
    if (pid < 0) {
        hloge("fork error: %d", errno);
        return -1;
    } else if (pid == 0) {
        // child proc
        hlogi("proc start/running, pid=%d", getpid());
        if (strlen(ctx->proctitle) != 0) {
            setproctitle(ctx->proctitle);
        }
        if (ctx->proc) {
            ctx->proc(ctx->userdata);
        }
        exit(0);
    } else if (pid > 0) {
        // parent proc
    }
    ctx->pid = pid;
    return pid;
}
#elif defined(_WIN32)
// win32 use multi-threads
#include <process.h>
inline int create_proc(proc_ctx_t* ctx) {
    HANDLE h = (HANDLE)_beginthread(ctx->proc, 0, ctx->userdata);
    if (h == NULL) {
        hloge("_beginthread error: %d", errno);
        return -1;
    }
    int tid = GetThreadId(h);
    ctx->pid = tid;
    hlogi("proc start/running, tid=%d", tid);
    return tid;
}
#endif

#endif // H_PROC_H_
