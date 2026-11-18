/* Linux epoll rejects regular files. For an already active socket, inject
 * a single failed MOD and let the kernel deliver its original read event. */
#define _GNU_SOURCE
#include "hloop.h"
#include "hbuf.h"
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

static int reject_mod_fd = -1;
static int reject_next_add;
static int ready_calls;
static int buffered_reads;

int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event) {
    if ((op == EPOLL_CTL_MOD && fd == reject_mod_fd) || (op == EPOLL_CTL_ADD && reject_next_add)) {
        reject_mod_fd = -1;
        reject_next_add = 0;
        errno = ENOSPC;
        return -1;
    }
    return (int)syscall(SYS_epoll_ctl, epfd, op, fd, event);
}

static void require(int ok, const char *message) {
    if (!ok) {
        fprintf(stderr, "read_registration_test: %s\n", message);
        exit(EXIT_FAILURE);
    }
}

static void unexpected_read(hio_t *io, void *buf, int len) {
    (void)io;
    (void)buf;
    (void)len;
    require(0, "rejected read dispatched a callback");
}

static void buffered_read(hio_t *io, void *buf, int len) {
    (void)io;
    require(len == 1 && *(char *)buf == 'b', "buffered read delivered the wrong bytes");
    ++buffered_reads;
}

static void original_ready(hio_t *io) {
    char byte;
    require(read(hio_fd(io), &byte, 1) == 1 && byte == 'r', "could not receive the original read event");
    ++ready_calls;
}

static void unexpected_ready(hio_t *io) {
    (void)io;
    require(0, "rejected upgrade replaced the active callback");
}

int main(void) {
    if (strcmp(hio_engine(), "epoll") != 0) return 0;
    hloop_t *loop = hloop_new(HLOOP_FLAG_RUN_ONCE);
    FILE *file = tmpfile();
    require(file != NULL, "could not create a regular file");
    hio_t *file_io = hio_get(loop, fileno(file));
    require(hio_read(file_io) < 0 && hio_events(file_io) == 0, "regular file read reported success");
    char readbuf[32] = "b";
    hio_set_readbuf(file_io, readbuf, sizeof(readbuf));
    hio_setcb_read(file_io, buffered_read);
    hio_get_readbuf(file_io)->tail = 1;
    require(hio_read(file_io) < 0 && buffered_reads == 0, "failed read consumed buffered input");
    require(hio_read_once(file_io) < 0 && hio_read_until_length(file_io, 2) < 0 && hio_read_until_delim(file_io, '\n') < 0,
            "failed framed read reported success");
    char replacement[32];
    require(hread(loop, fileno(file), replacement, sizeof(replacement), unexpected_read) == NULL, "hread reported success");
    require(hio_get_readbuf(file_io)->base == readbuf, "failed hread retained the supplied buffer");

    require(hio_read_until_length(file_io, 1) == 1 && buffered_reads == 1, "failed hread lost buffered input or its callback");

    int pair[2];
    require(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0, "could not open socket pair");
    hio_t *io = hio_get(loop, pair[0]);
    hio_set_readbuf(io, readbuf, sizeof(readbuf));
    hio_get_readbuf(io)->tail = 1;
    require(hread(loop, pair[0], NULL, 0, buffered_read) == io && buffered_reads == 2, "hread did not immediately deliver buffered input");
    require(hio_add(io, original_ready, HV_READ) == 0, "could not register original reader");
    reject_mod_fd = pair[0];
    require(hio_add(io, unexpected_ready, HV_RDWR) < 0 && hio_events(io) == HV_READ, "failed upgrade changed interests");
    require(write(pair[1], "r", 1) == 1 && hloop_run(loop) == 0 && ready_calls == 1, "read interest was lost");

    hloop_free(&loop);
    fclose(file);
    close(pair[1]);

    loop = hloop_new(HLOOP_FLAG_RUN_ONCE | HLOOP_FLAG_QUIT_WHEN_NO_ACTIVE_EVENTS);
    reject_next_add = 1;
    require(hloop_run(loop) < 0, "loop started without a working wake reader");
    require(hloop_run(loop) == 0, "could not retry loop startup");
    hloop_free(&loop);
    return 0;
}
