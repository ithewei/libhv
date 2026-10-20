/* Exercise failed Windows backend initialization and retry. Only the first
 * creation fails artificially; successful ports and cleanup use real wepoll. */
#include "hevent.h"
#include "wepoll/wepoll.h"

static unsigned int create_calls;
static int fail_next_create;

static HANDLE test_epoll_create(int size) {
    ++create_calls;
    if (fail_next_create) {
        fail_next_create = 0;
        errno = ENOMEM;
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    return epoll_create(size);
}

/* Keep the backend under test separate from the linked library's watcher. */
#define epoll_create test_epoll_create
#define iowatcher_init test_iowatcher_init
#define iowatcher_cleanup test_iowatcher_cleanup
#define iowatcher_add_event test_iowatcher_add_event
#define iowatcher_del_event test_iowatcher_del_event
#define iowatcher_poll_events test_iowatcher_poll_events
#include "../event/epoll.c"
#undef epoll_create
#undef iowatcher_init
#undef iowatcher_cleanup
#undef iowatcher_add_event
#undef iowatcher_del_event
#undef iowatcher_poll_events

/* Assertions must remain active in optimized builds. */
static void require(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "epoll_init_test: %s (errno=%d, winerror=%lu)\n", message, errno, (unsigned long)GetLastError());
        exit(EXIT_FAILURE);
    }
}

int main(void) {
    hloop_t loop;
    HANDLE port;
    DWORD flags;
    memset(&loop, 0, sizeof(loop));

    fail_next_create = 1;
    require(test_iowatcher_init(&loop) == -1, "failed epoll_create was reported as successful initialization");
    require(loop.iowatcher == NULL, "failed initialization published an invalid context");
    require(create_calls == 1, "creation failure did not reach the backend");

    require(test_iowatcher_init(&loop) == 0, "initialization did not recover after creation failure");
    require(create_calls == 2 && loop.iowatcher != NULL, "retry did not create a new port");
    port = ((epoll_ctx_t *)loop.iowatcher)->epfd;
    require(port != NULL && GetHandleInformation(port, &flags), "retry did not create a valid native port");
    require(test_iowatcher_init(&loop) == 0 && create_calls == 2, "initialization was not idempotent");
    require(test_iowatcher_cleanup(&loop) == 0 && loop.iowatcher == NULL, "cleanup retained the port context");
    require(!GetHandleInformation(port, &flags) && GetLastError() == ERROR_INVALID_HANDLE, "cleanup did not close the native port");
    require(test_iowatcher_cleanup(&loop) == 0, "repeated cleanup failed");

    require(test_iowatcher_init(&loop) == 0 && create_calls == 3, "ordinary creation after cleanup failed");
    port = ((epoll_ctx_t *)loop.iowatcher)->epfd;
    require(port != NULL && GetHandleInformation(port, &flags), "ordinary creation did not produce a valid native port");
    require(test_iowatcher_cleanup(&loop) == 0 && loop.iowatcher == NULL, "ordinary port cleanup failed");
    require(!GetHandleInformation(port, &flags) && GetLastError() == ERROR_INVALID_HANDLE, "ordinary cleanup did not close the native port");
    puts("epoll_init_test passed");
    return 0;
}
