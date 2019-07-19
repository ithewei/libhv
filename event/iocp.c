#include "iowatcher.h"

#ifdef EVENT_IOCP
#include "hplatform.h"
#include "hdef.h"

typedef struct iocp_ctx_s {
    HANDLE      iocp;
} iocp_ctx_t;

int iowatcher_init(hloop_t* loop) {
    if (loop->iowatcher)    return 0;
    iocp_ctx_t* iocp_ctx = (iocp_ctx_t*)malloc(sizeof(iocp_ctx_t));
    iocp_ctx->iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    loop->iowatcher = iocp_ctx;
    return 0;
}

int iowatcher_cleanup(hloop_t* loop) {
    if (loop->iowatcher == NULL) return 0;
    iocp_ctx_t* iocp_ctx = (iocp_ctx_t*)loop->iowatcher;
    CloseHandle(iocp_ctx->iocp);
    SAFE_FREE(loop->iowatcher);
    return 0;
}

int iowatcher_add_event(hloop_t* loop, int fd, int events) {
    if (loop->iowatcher == NULL) {
        iowatcher_init(loop);
    }
    iocp_ctx_t* iocp_ctx = (iocp_ctx_t*)loop->iowatcher;
    HANDLE h = CreateIoCompletionPort((HANDLE)fd, iocp_ctx->iocp, (ULONG_PTR)events, 0);
    return 0;
}

int iowatcher_del_event(hloop_t* loop, int fd, int events) {
    return 0;
}

int iowatcher_poll_events(hloop_t* loop, int timeout) {
    if (loop->iowatcher == NULL) return 0;
    iocp_ctx_t* iocp_ctx = (iocp_ctx_t*)loop->iowatcher;
    DWORD bytes = 0;
    ULONG_PTR key = 0;
    LPOVERLAPPED povlp = NULL;
    timeout = 3000;
    BOOL bRet = GetQueuedCompletionStatus(iocp_ctx->iocp, &bytes, &key, &povlp, timeout);
    int err = 0;
    if (bRet == 0) {
        err = GetLastError();
    }
    if (err) {
        if (err == ERROR_NETNAME_DELETED ||
            err == ERROR_OPERATION_ABORTED) {
            return 0;
        }
        if (povlp == NULL) {
            if (err == WAIT_TIMEOUT) return 0;
            return -1;
        }
    }
    if (povlp == NULL) {
        return -1;
    }
    if (key == NULL) {
        return -1;
    }
    ULONG_PTR revents = key;
    return 1;
}
#endif
