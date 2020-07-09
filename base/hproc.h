#ifndef HV_PROC_H_
#define HV_PROC_H_

#include "hplatform.h"

typedef struct proc_ctx_s {
    pid_t           pid; // tid in win32
    procedure_t     init;
    void*           init_userdata;
    procedure_t     proc;
    void*           proc_userdata;
    procedure_t     exit;
    void*           exit_userdata;
} proc_ctx_t;

#ifdef OS_UNIX
// unix use multi-processes
static inline int hproc_spawn(proc_ctx_t* ctx) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    } else if (pid == 0) {
        // child proc
        ctx->pid = getpid();
        if (ctx->init) {
            ctx->init(ctx->init_userdata);
        }
        if (ctx->proc) {
            ctx->proc(ctx->proc_userdata);
        }
        if (ctx->exit) {
            ctx->exit(ctx->exit_userdata);
        }
        exit(0);
    } else if (pid > 0) {
        // parent proc
        ctx->pid = pid;
    }
    return pid;
}
#elif defined(OS_WIN)
// win32 use multi-threads
static void win_thread(void* userdata) {
    proc_ctx_t* ctx = (proc_ctx_t*)userdata;
    if (ctx->init) {
        ctx->init(ctx->init_userdata);
    }
    if (ctx->proc) {
        ctx->proc(ctx->proc_userdata);
    }
    if (ctx->exit) {
        ctx->exit(ctx->exit_userdata);
    }
}
static inline int hproc_spawn(proc_ctx_t* ctx) {
    HANDLE h = (HANDLE)_beginthread(win_thread, 0, ctx);
    if (h == NULL) {
        return -1;
    }
    int tid = GetThreadId(h);
    ctx->pid = tid;
    return tid;
}
#endif

#endif // HV_PROC_H_
