#include "iowatcher.h"

#ifdef EVENT_IOCP
#include "hplatform.h"
#include "hdef.h"

#include "hevent.h"
#include "overlapio.h"

typedef struct iocp_ctx_s {
    HANDLE      iocp;
} iocp_ctx_t;

int iowatcher_init(hloop_t* loop) {
    if (loop->iowatcher)    return 0;
    iocp_ctx_t* iocp_ctx;
    HV_ALLOC_SIZEOF(iocp_ctx);
    iocp_ctx->iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    loop->iowatcher = iocp_ctx;
    return 0;
}

int iowatcher_cleanup(hloop_t* loop) {
    if (loop->iowatcher == NULL) return 0;
    iocp_ctx_t* iocp_ctx = (iocp_ctx_t*)loop->iowatcher;
    CloseHandle(iocp_ctx->iocp);
    HV_FREE(loop->iowatcher);
    return 0;
}

int iowatcher_add_event(hloop_t* loop, int fd, int events) {
    if (loop->iowatcher == NULL) {
        iowatcher_init(loop);
    }
    iocp_ctx_t* iocp_ctx = (iocp_ctx_t*)loop->iowatcher;
    hio_t* io = loop->ios.ptr[fd];
    if (io && io->events == 0 && events != 0) {
        CreateIoCompletionPort((HANDLE)fd, iocp_ctx->iocp, 0, 0);
    }
    return 0;
}

int iowatcher_del_event(hloop_t* loop, int fd, int events) {
    hio_t* io = loop->ios.ptr[fd];
    if ((io->events & ~events) == 0) {
        CancelIo((HANDLE)fd);
    }
    return 0;
}

int iowatcher_poll_events(hloop_t* loop, int timeout) {
    if (loop->iowatcher == NULL) return 0;
    iocp_ctx_t* iocp_ctx = (iocp_ctx_t*)loop->iowatcher;
    DWORD bytes = 0;
    ULONG_PTR key = 0;
    LPOVERLAPPED povlp = NULL;
    BOOL bRet = GetQueuedCompletionStatus(iocp_ctx->iocp, &bytes, &key, &povlp, timeout);
    int err = 0;
    if (povlp == NULL) {
        err = WSAGetLastError();
        if (err == WAIT_TIMEOUT || ERROR_NETNAME_DELETED || ERROR_OPERATION_ABORTED) {
            return 0;
        }
        return -err;
    }
    hoverlapped_t* hovlp = (hoverlapped_t*)povlp;
    hio_t* io = hovlp->io;
    if (bRet == FALSE) {
        err = WSAGetLastError();
        printd("iocp ret=%d err=%d bytes=%u\n", bRet, err, bytes);
        // NOTE: when ConnectEx failed, err != 0
        hovlp->error = err;
    }
    // NOTE: when WSASend/WSARecv disconnect, bytes = 0
    hovlp->bytes = bytes;
    io->hovlp = hovlp;
    io->revents |= hovlp->event;
    EVENT_PENDING(hovlp->io);
    return 1;
}
#endif
