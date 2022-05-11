#include "hv/hloop.h"
#include "hv/hbase.h"
#include "hv/hlog.h"
#include "hv/nlog.h"
#include "hv/hsocket.h"
#include "hv/ThreadLocalStorage.h"

#include "hvpp/inc/hvpp.h"

void mylogger(int loglevel, const char *buf, int len) {
    if (loglevel >= LOG_LEVEL_ERROR) {
        stderr_logger(loglevel, buf, len);
    }

    if (loglevel >= LOG_LEVEL_INFO) {
        file_logger(loglevel, buf, len);
    }

    network_logger(loglevel, buf, len);
}

void on_idle(hidle_t *idle) {
    printf("on_idle: event_id=%llu\tpriority=%d\tuserdata=%ld\n",
           LLU(hevent_id(idle)), hevent_priority(idle), (long)(intptr_t)(hevent_userdata(idle)));
}

void on_timer(htimer_t *timer) {
    hloop_t *loop = hevent_loop(timer);
    printf("on_timer: event_id=%llu\tpriority=%d\tuserdata=%ld\ttime=%llus\thrtime=%lluus\n",
           LLU(hevent_id(timer)), hevent_priority(timer), (long)(intptr_t)(hevent_userdata(timer)),
           LLU(hloop_now(loop)), LLU(hloop_now_hrtime(loop)));
}

void cron_hourly(htimer_t *timer) {
    time_t tt;
    time(&tt);
    printf("cron_hourly: %s\n", ctime(&tt));
}

void timer_write_log(htimer_t *timer) {
    static int cnt = 0;
    hlogd("[%d] Do you recv me?", ++cnt);
    hlogi("[%d] Do you recv me?", ++cnt);
    hloge("[%d] Do you recv me?", ++cnt);
}

void on_stdin(hio_t *io, void *buf, int readbytes) {
    printf("on_stdin fd=%d readbytes=%d\n", hio_fd(io), readbytes);
    printf("> %s\n", (char *)buf);
    if (strncmp((char *)buf, "quit", 4) == 0) {
        hloop_stop(hevent_loop(io));
    }
}

void on_custom_events(hevent_t *ev) {
    printf("on_custom_events event_type=%d userdata=%p\n", static_cast<int>(ev->event_type), (ev->userdata));
}

static void on_close(hio_t *io) {
    printf("on_close fd=%d error=%d\n", hio_fd(io), hio_error(io));
}

static void on_recv(hio_t *io, void *buf, int readbytes) {
    printf("on_recv fd=%d readbytes=%d\n", hio_fd(io), readbytes);
    char localaddrstr[SOCKADDR_STRLEN] = {0};
    char peeraddrstr[SOCKADDR_STRLEN] = {0};
    printf("[%s] <=> [%s]\n", SOCKADDR_STR(hio_localaddr(io), localaddrstr), SOCKADDR_STR(hio_peeraddr(io), peeraddrstr));
    printf("< %.*s", readbytes, (char *)buf);
    // echo
    printf("> %.*s", readbytes, (char *)buf);
    hio_write(io, buf, readbytes);
}



static void on_accept(hio_t *io) {
    printf("on_accept connfd=%d\n", hio_fd(io));
    char localaddrstr[SOCKADDR_STRLEN] = {0};
    char peeraddrstr[SOCKADDR_STRLEN] = {0};
    printf("accept connfd=%d [%s] <= [%s]\n", hio_fd(io), SOCKADDR_STR(hio_localaddr(io), localaddrstr),
           SOCKADDR_STR(hio_peeraddr(io), peeraddrstr));

    hio_setcb_close(io, on_close);
    hio_setcb_read(io, on_recv);
    hio_read_start(io);
}

int main() {
    // memcheck atexit
    HV_MEMCHECK;

    auto hloop = hvpp::HLoop();
    auto &listenio = hloop.CreateTcpServer("0.0.0.0", 1234, [](hvpp::HIo & io) {
        printf("accepted\n");
        io.onClose = [](hvpp::HIo & io) {
            printf("clnt closed\n");
        };
        io.onRead = [](hvpp::HIo & io, HBuf & buf) {
            printf("%s", buf.base);
            io.Write("abc", 4);
        };
        io.onWrite = [](hvpp::HIo & io, const HBuf & buf) {
            printf("clnt write: %s\n", buf.base);
        };
    });

    listenio.onClose = [](hvpp::HIo & io) {
        printf("svr closed\n");
    };

    auto &timer = hloop.AddTimer([&hloop](hvpp::HTimer &timer) { 
        printf("timeout\n");
        hloop.Stop();
    }, 10000, 1);

    hloop.Run();

    return 0;

    //auto loop = hloop_new(0);
    //auto listenio = hloop_create_tcp_server(loop, "0.0.0.0", 1234, on_accept);

    // test idle and priority
    //for (int i = HEVENT_LOWEST_PRIORITY; i <= HEVENT_HIGHEST_PRIORITY; ++i) {
    //    hidle_t* idle = hidle_add(loop, on_idle, 10);
    //    hevent_set_priority(idle, i);
    //}

    // test timer timeout
    //for (int i = 1; i <= 10; ++i) {
    //    htimer_t* timer = htimer_add(loop, on_timer, i*1000, 3);
    //    hevent_set_userdata(timer, (void*)(intptr_t)i);
    //}

    //// test timer period
    //int minute = time(NULL)%3600/60;
    //htimer_add_period(loop, cron_hourly, minute+1, -1, -1, -1, -1, INFINITE);

    // test network_logger
    //htimer_add(loop, timer_write_log, 1, INFINITE);
    //logger_set_handler(hlog, mylogger);
    //hlog_set_file("loop.log");
//#ifndef _MSC_VER
//    logger_enable_color(hlog, 1);
//#endif
    //nlog_listen(loop, DEFAULT_LOG_PORT);

    //// test nonblock stdin
    //printf("input 'quit' to quit loop\n");
    //char buf[64];
    //hread(loop, 0, buf, sizeof(buf), on_stdin);

    //// test custom_events
    //for (int i = 0; i < 10; ++i) {
    //    hevent_t ev;
    //    memset(&ev, 0, sizeof(ev));
    //    ev.event_type = (hevent_type_e)(HEVENT_TYPE_CUSTOM + i);
    //    ev.cb = on_custom_events;
    //    ev.userdata = reinterpret_cast<void*>(i);
    //    hloop_post_event(loop, &ev);
    //}

    //hloop_run(loop);
    //hloop_free(&loop);
    return 0;
}

