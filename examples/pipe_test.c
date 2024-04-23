/*
 * hio_create_pipe test
 *
 * @build make examples
 * @test  bin/pipe_test
 *
 */

#include "hloop.h"
#include "htime.h"

static hio_t* pipeio[2] = { NULL, NULL };

static void on_read(hio_t* io, void* buf, int readbytes) {
    printf("< %.*s\n", readbytes, (char*)buf);
}

static void on_timer_write(htimer_t* timer) {
    char str[DATETIME_FMT_BUFLEN] = {0};
    datetime_t dt = datetime_now();
    datetime_fmt(&dt, str);
    hio_write(pipeio[1], str, strlen(str));
}

static void on_timer_stop(htimer_t* timer) {
    hio_close(pipeio[0]);
    hio_close(pipeio[1]);
    hloop_stop(hevent_loop(timer));
}

int main(int argc, char** argv) {
    hloop_t* loop = hloop_new(0);

    int ret = hio_create_pipe(loop, pipeio);
    if (ret != 0) {
        printf("hio_create_pipe failed!\n");
        return -10;
    }
    printf("pipefd %d<=>%d\n", hio_fd(pipeio[0]), hio_fd(pipeio[1]));

    hio_setcb_read(pipeio[0], on_read);
    hio_read(pipeio[0]);

    htimer_add(loop, on_timer_write, 1000, INFINITE);

    htimer_add(loop, on_timer_stop, 10000, 1);

    hloop_run(loop);
    hloop_free(&loop);
    return 0;
}
