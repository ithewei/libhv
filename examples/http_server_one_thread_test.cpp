#include "HttpServer.h"
#include "hthread.h"    // import hv_gettid
#include "hasync.h"     // import hv::async
#include "hv/EventLoop.h"
#include "hlog.h"

using namespace hv;

/*
 *
 * @build   cmake -Bbuild && cmake --build build -j
 *
 * @server  ./build/bin/http_server_one_thread_test 8086
 *
 * @client  curl -v http://127.0.0.1:8086/ping

 */

void mylogger(int loglevel, const char* buf, int len) {
    if (loglevel >= LOG_LEVEL_INFO) {
        stdout_logger(loglevel, buf, len);
    }

    if (loglevel >= LOG_LEVEL_INFO) {
        file_logger(loglevel, buf, len);
    }
}

int main(int argc, char** argv) {
    HV_MEMCHECK;

    int port = 0;
    if (argc > 1) {
        port = atoi(argv[1]);
    }
    if (port == 0) port = 8086;

    hlog_set_handler(mylogger);
    hlog_set_file("loop.log");
    hlog_set_format(DEFAULT_LOG_FORMAT);
#ifndef _MSC_VER
    logger_enable_color(hlog, 1);
#endif

    HttpService router;

    // curl -v http://ip:port/ping
    router.GET("/ping", [](HttpRequest* req, HttpResponse* resp) {
        return resp->String("pong");
    });

    HttpServer server;
    server.service = &router;
    server.port = port;

    server.start_before_run();
    EventLoopPtr loop = server.loop(0);

    // add custom event to loop
    loop->setInterval(1000, [](TimerID timerID) {
        static int cnt = 0;
        hlogd("[%d] Do you recv me?", ++cnt);
        hlogi("[%d] Do you recv me?", ++cnt);
        hloge("[%d] Do you recv me?", ++cnt);
    });

    server.run_with_start();
    hv::async::cleanup();
    return 0;
}
