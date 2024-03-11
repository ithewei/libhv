#define _POSIX_SOURCE

#include "list.h"
#include "hevent.h"

#define SIGNAL_ENTRY(p)           list_entry(p, hsig_t, self_signal_node)

#ifndef SA_RESTART
#define SA_RESTART 0x1000000
#endif

#ifdef OS_LINUX
/*
* The fd here cannot use global content, it must be one for each loop, because it is even when executing sig_event_cb, and each loop will read
* The content written in fd, if all the content is read by the first loop, the remaining loops will not be able to read the information.
* Therefore, there is a limitation in current use. A process can only apply for loop_new once. If there are multiple loop_new scenarios in the future,
* This business logic needs to be optimized.
*/
static int sig_write_fd = -1;
static int pair[2];
static bool sig_init[_NSIG];

void sig_handler(int sig)
{
    int save_errno = errno;
    char signum = (char)sig;
    int n = write(sig_write_fd, &signum, 1);
    printf("write %d signals in sig_handler\n", n);

    errno = save_errno;
}

void sig_event_cb(hio_t *io)
{
    printf("in sig_event_cb\n");
    char signals[1024];
    int n = 0;
    int ncaught[_NSIG];
    int fd = io->fd;
    hloop_t *loop = hio_context(io);
    struct list_head *events_at_sig = NULL;
    struct list_head *ev_node = NULL;
    memset(signals, 0, sizeof(signals));
    memset(ncaught, 0, sizeof(ncaught));

    if (loop == NULL) {
        return;
    }

    while (true) {
        n = read(fd, signals, sizeof(signals));
        if (n <= 0) {
            break;
        }
        for (int i = 0; i < n; ++i) {
        char sig = signals[i];
            if (sig < _NSIG)
                ncaught[sig]++;
        }
    }

    for (int i = 0; i < _NSIG; i++) {
        if (ncaught[i] > 0) {
            events_at_sig = &(loop->signal_events_head[i]);
            if (!list_empty(events_at_sig)) {
                ev_node = events_at_sig->next;
                while (ev_node != events_at_sig) {
                    hsig_t *signal = SIGNAL_ENTRY(ev_node);
                    signal->num_calls = ncaught[i];
                    // In this part, there should be no need to add events to the awaken_signal_events_head list.
                    // Instead, they should be directly added to the existing loop's pending.
                    // According to the logic in hloop.c, various types of events should not invoke their callbacks when processed but should be placed in the pending queue. The callbacks for these events should be invoked collectively after all events are processed (except for timeouts).
                    // Therefore, the existing logic in hloop should handle the current awaken_signal_events_head.
                    // However, it's necessary to consider the case where num_calls is greater than one. Multiple pending queues may be needed.
                    for (int j = 0; j < signal->num_calls; j++) {
                        EVENT_PENDING(signal);
                    }
                    ev_node = ev_node->next;
                }
            }
        }
    }

    printf("exit sig_event_cb\n");
}

/***
  * This function is mainly used to add a semaphore. The signal_events_head in the loop is the maximum value of _NSIG.
  * Every time a semaphore is added, it needs to be appended to the signal_events_head[sig] linked list.
  * When the semaphore is processed, all registered cbs will be traversed from the corresponding linked list and the callback will be called.
  * Use the same pair globally, and all loops are registered to listen to pair[0]. When an event is written, it is judged whether it needs to be processed.
  *
  * */
hsig_t* hsig_add(hloop_t* loop, hsig_cb cb, uint32_t sig)
{
    printf("hsig_add sig=%d\n", sig);

    hsig_t* signal;
    HV_ALLOC_SIZEOF(signal);
    struct list_head *events_at_sig = NULL;
    struct sigaction sa;

    if (-1 == sig_write_fd) {
        socketpair(AF_UNIX, SOCK_STREAM, 0, pair);
        fcntl(pair[0], F_SETFL, O_NONBLOCK);
        fcntl(pair[1], F_SETFL, O_NONBLOCK);
        fcntl(pair[0], F_SETFD, FD_CLOEXEC);
        fcntl(pair[1], F_SETFD, FD_CLOEXEC);
        sig_write_fd = pair[1];

        memset(sig_init, 0, sizeof(sig_init));
    }
    // 每个信号量只注册一次
    if (false == sig_init[sig]) {
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = sig_handler;
        sa.sa_flags |= SA_RESTART;
        sigfillset(&sa.sa_mask);
        sigaction(sig, &sa, NULL);
        sig_init[sig] = true;
    }

    if (loop->enable_signal == 0) {
        printf("Enable the signal\n");
        loop->enable_signal = 1;
        for (int i = 0; i < _NSIG; i++) {
            list_init(&(loop->signal_events_head[i]));
        }
        loop->signal_io_watcher = hio_get(loop, pair[0]);
        hio_set_context(loop->signal_io_watcher, loop);
        hio_add(loop->signal_io_watcher, (hio_cb)sig_event_cb, HV_READ);
    }
    events_at_sig = &(loop->signal_events_head[sig]);
    signal->loop = loop;
    signal->event_type = HEVENT_TYPE_SIGNAL;
    signal->priority = HEVENT_LOWEST_PRIORITY;
    list_add(&signal->self_signal_node, events_at_sig);
    EVENT_ADD(loop, signal, cb);
    loop->nsignals++;

    return signal;
}

static void __hsig_del(hsig_t* signal)
{
    if (signal->destroy) {
        return;
    }
    signal->destroy = 1;
    list_del(&signal->self_signal_node);
    signal->loop->nsignals--;
}

void hsig_del(hsig_t* signal)
{
    if (!signal->active) {
        return;
    }
    __hsig_del(signal);
    EVENT_DEL(signal);
}
#endif