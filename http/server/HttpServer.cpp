#include "HttpServer.h"

#include "hmain.h" // import master_workers_run
#include "herr.h"
#include "hlog.h"
#include "htime.h"

#include "EventLoop.h"
using namespace hv;

#include "HttpHandler.h"

static void on_accept(hio_t* io);
static void on_recv(hio_t* io, void* _buf, int readbytes);
static void on_close(hio_t* io);

struct HttpServerPrivdata {
    std::vector<EventLoopPtr>       loops;
    std::vector<hthread_t>          threads;
    std::mutex                      mutex_;
    std::shared_ptr<HttpService>    service;
    FileCache                       filecache;
};

static void on_recv(hio_t* io, void* buf, int readbytes) {
    // printf("on_recv fd=%d readbytes=%d\n", hio_fd(io), readbytes);
    HttpHandler* handler = (HttpHandler*)hevent_userdata(io);
    assert(handler != NULL);

    int nfeed = handler->FeedRecvData((const char*)buf, readbytes);
    if (nfeed != readbytes) {
        hio_close(io);
        return;
    }
}

static void on_close(hio_t* io) {
    HttpHandler* handler = (HttpHandler*)hevent_userdata(io);
    if (handler == NULL) return;

    hevent_set_userdata(io, NULL);
    delete handler;

    EventLoop* loop = currentThreadEventLoop;
    if (loop) {
        --loop->connectionNum;
    }
}

static void on_accept(hio_t* io) {
    http_server_t* server = (http_server_t*)hevent_userdata(io);
    HttpService* service = server->service;
    /*
    printf("on_accept connfd=%d\n", hio_fd(io));
    char localaddrstr[SOCKADDR_STRLEN] = {0};
    char peeraddrstr[SOCKADDR_STRLEN] = {0};
    printf("accept connfd=%d [%s] <= [%s]\n", hio_fd(io),
            SOCKADDR_STR(hio_localaddr(io), localaddrstr),
            SOCKADDR_STR(hio_peeraddr(io), peeraddrstr));
    */

    EventLoop* loop = currentThreadEventLoop;
    if (loop->connectionNum >= server->worker_connections) {
        hlogw("over worker_connections");
        hio_close(io);
        return;
    }
    ++loop->connectionNum;

    hio_setcb_close(io, on_close);
    hio_setcb_read(io, on_recv);
    hio_read(io);
    if (service->keepalive_timeout > 0) {
        hio_set_keepalive_timeout(io, service->keepalive_timeout);
    }

    // new HttpHandler, delete on_close
    HttpHandler* handler = new HttpHandler(io);
    // ssl
    handler->ssl = hio_is_ssl(io);
    // ip:port
    sockaddr_u* peeraddr = (sockaddr_u*)hio_peeraddr(io);
    sockaddr_ip(peeraddr, handler->ip, sizeof(handler->ip));
    handler->port = sockaddr_port(peeraddr);
    // http service
    handler->service = service;
    // websocket service
    handler->ws_service = server->ws;
    // FileCache
    HttpServerPrivdata* privdata = (HttpServerPrivdata*)server->privdata;
    handler->files = &privdata->filecache;
    hevent_set_userdata(io, handler);
}

static void loop_thread(void* userdata) {
    http_server_t* server = (http_server_t*)userdata;
    HttpService* service = server->service;

    auto loop = std::make_shared<EventLoop>();
    hloop_t* hloop = loop->loop();
    // http
    if (server->listenfd[0] >= 0) {
        hio_t* listenio = haccept(hloop, server->listenfd[0], on_accept);
        hevent_set_userdata(listenio, server);
    }
    // https
    if (server->listenfd[1] >= 0) {
        hio_t* listenio = haccept(hloop, server->listenfd[1], on_accept);
        hevent_set_userdata(listenio, server);
        hio_enable_ssl(listenio);
        if (server->ssl_ctx) {
            hio_set_ssl_ctx(listenio, server->ssl_ctx);
        }
    }

    HttpServerPrivdata* privdata = (HttpServerPrivdata*)server->privdata;
    privdata->mutex_.lock();
    if (privdata->loops.size() == 0) {
        // NOTE: fsync logfile when idle
        hlog_disable_fsync();
        hidle_add(hloop, [](hidle_t*) {
            hlog_fsync();
        }, INFINITE);

        // NOTE: add timer to update s_date every 1s
        htimer_add(hloop, [](htimer_t* timer) {
            gmtime_fmt(hloop_now(hevent_loop(timer)), HttpMessage::s_date);
        }, 1000);

        // document_root
        if (service->document_root.size() > 0 && service->GetStaticFilepath("/").empty()) {
            service->Static("/", service->document_root.c_str());
        }

        // FileCache
        FileCache* filecache = &privdata->filecache;
        filecache->stat_interval = service->file_cache_stat_interval;
        filecache->expired_time = service->file_cache_expired_time;
        if (filecache->expired_time > 0) {
            // NOTE: add timer to remove expired file cache
            htimer_t* timer = htimer_add(hloop, [](htimer_t* timer) {
                FileCache* filecache = (FileCache*)hevent_userdata(timer);
                filecache->RemoveExpiredFileCache();
            },  filecache->expired_time * 1000);
            hevent_set_userdata(timer, filecache);
        }
    }
    privdata->loops.push_back(loop);
    privdata->mutex_.unlock();

    hlogi("EventLoop started, pid=%ld tid=%ld", hv_getpid(), hv_gettid());
    if (server->onWorkerStart) {
        loop->queueInLoop([server](){
            server->onWorkerStart();
        });
    }

    loop->run();

    if (server->onWorkerStop) {
        server->onWorkerStop();
    }
    hlogi("EventLoop stopped, pid=%ld tid=%ld", hv_getpid(), hv_gettid());
}

/* @workflow:
 * http_server_run -> Listen -> master_workers_run / hthread_create ->
 * loop_thread -> accept -> EventLoop::run ->
 * on_accept -> new HttpHandler -> hio_read ->
 * on_recv -> HttpHandler::FeedRecvData ->
 * on_close -> delete HttpHandler
 */
int http_server_run(http_server_t* server, int wait) {
    // http_port
    if (server->port > 0) {
        server->listenfd[0] = Listen(server->port, server->host);
        if (server->listenfd[0] < 0) return server->listenfd[0];
        hlogi("http server listening on %s:%d", server->host, server->port);
    }
    // https_port
    if (server->https_port > 0 && HV_WITH_SSL) {
        server->listenfd[1] = Listen(server->https_port, server->host);
        if (server->listenfd[1] < 0) return server->listenfd[1];
        hlogi("https server listening on %s:%d", server->host, server->https_port);
    }
    // SSL_CTX
    if (server->listenfd[1] >= 0) {
        if (server->ssl_ctx == NULL) {
            server->ssl_ctx = hssl_ctx_instance();
        }
        if (server->ssl_ctx == NULL) {
            hloge("new SSL_CTX failed!");
            return ERR_NEW_SSL_CTX;
        }
#ifdef WITH_NGHTTP2
#ifdef WITH_OPENSSL
        static unsigned char s_alpn_protos[] = "\x02h2\x08http/1.1\x08http/1.0\x08http/0.9";
        hssl_ctx_set_alpn_protos(server->ssl_ctx, s_alpn_protos, sizeof(s_alpn_protos) - 1);
#endif
#endif
    }

    HttpServerPrivdata* privdata = new HttpServerPrivdata;
    server->privdata = privdata;
    if (server->service == NULL) {
        privdata->service = std::make_shared<HttpService>();
        server->service = privdata->service.get();
    }

    if (server->worker_processes) {
        // multi-processes
        return master_workers_run(loop_thread, server, server->worker_processes, server->worker_threads, wait);
    }
    else {
        // multi-threads
        if (server->worker_threads == 0) server->worker_threads = 1;
        for (int i = wait ? 1 : 0; i < server->worker_threads; ++i) {
            hthread_t thrd = hthread_create((hthread_routine)loop_thread, server);
            privdata->threads.push_back(thrd);
        }
        if (wait) {
            loop_thread(server);
        }
        return 0;
    }
}

/* @workflow:
 * http_server_stop -> EventLoop::stop -> hthread_join
 */
int http_server_stop(http_server_t* server) {
    HttpServerPrivdata* privdata = (HttpServerPrivdata*)server->privdata;
    if (privdata == NULL) return 0;

#ifdef OS_UNIX
    if (server->worker_processes) {
        signal_handle("stop");
        return 0;
    }
#endif

    // wait for all threads started and all loops running
    while (1) {
        hv_delay(1);
        std::lock_guard<std::mutex> locker(privdata->mutex_);
        // wait for all loops created
        if (privdata->loops.size() < server->worker_threads) {
            continue;
        }
        // wait for all loops running
        bool all_loops_running = true;
        for (auto& loop : privdata->loops) {
            if (loop->status() < hv::Status::kRunning) {
                all_loops_running = false;
                break;
            }
        }
        if (all_loops_running) break;
    }

    // stop all loops
    for (auto& loop : privdata->loops) {
        loop->stop();
    }

    // join all threads
    for (auto& thrd : privdata->threads) {
        hthread_join(thrd);
    }

    if (server->alloced_ssl_ctx && server->ssl_ctx) {
        hssl_ctx_free(server->ssl_ctx);
        server->alloced_ssl_ctx = 0;
        server->ssl_ctx = NULL;
    }

    delete privdata;
    server->privdata = NULL;
    return 0;
}
