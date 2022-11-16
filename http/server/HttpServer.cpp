#include "HttpServer.h"

#include "hv.h"
#include "hssl.h"
#include "hmain.h"

#include "httpdef.h"
#include "http2def.h"
#include "wsdef.h"

#include "EventLoop.h"
using namespace hv;

#include "HttpHandler.h"

#define MIN_HTTP_REQUEST        "GET / HTTP/1.1\r\n\r\n"
#define MIN_HTTP_REQUEST_LEN    14 // exclude CRLF

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

static void on_recv(hio_t* io, void* _buf, int readbytes) {
    // printf("on_recv fd=%d readbytes=%d\n", hio_fd(io), readbytes);
    const char* buf = (const char*)_buf;
    HttpHandler* handler = (HttpHandler*)hevent_userdata(io);
    assert(handler != NULL);

    // HttpHandler::Init(http_version) -> upgrade ? SwitchHTTP2 / SwitchWebSocket
    // on_recv -> FeedRecvData -> HttpRequest
    // onComplete -> HandleRequest -> HttpResponse -> while (GetSendData) -> send

    HttpHandler::ProtocolType protocol = handler->protocol;
    if (protocol == HttpHandler::UNKNOWN) {
        int http_version = 1;
#if WITH_NGHTTP2
        if (strncmp((char*)buf, HTTP2_MAGIC, MIN(readbytes, HTTP2_MAGIC_LEN)) == 0) {
            http_version = 2;
        }
#else
        // check request-line
        if (readbytes < MIN_HTTP_REQUEST_LEN) {
            hloge("[%s:%d] http request-line too small", handler->ip, handler->port);
            hio_close(io);
            return;
        }
        for (int i = 0; i < MIN_HTTP_REQUEST_LEN; ++i) {
            if (!IS_GRAPH(buf[i])) {
                hloge("[%s:%d] http request-line not plain", handler->ip, handler->port);
                hio_close(io);
                return;
            }
        }
#endif
        if (!handler->Init(http_version, io)) {
            hloge("[%s:%d] unsupported HTTP%d", handler->ip, handler->port, http_version);
            hio_close(io);
            return;
        }
    }

    int nfeed = handler->FeedRecvData(buf, readbytes);
    if (nfeed != readbytes) {
        hio_close(io);
        return;
    }

    hloop_t* loop = hevent_loop(io);
    HttpParser* parser = handler->parser.get();
    HttpRequest* req = handler->req.get();
    HttpResponse* resp = handler->resp.get();

    if (handler->proxy) {
        return;
    }

    if (protocol == HttpHandler::WEBSOCKET) {
        return;
    }

    if (parser->WantRecv()) {
        return;
    }

    // Server:
    static char s_Server[64] = {'\0'};
    if (s_Server[0] == '\0') {
        snprintf(s_Server, sizeof(s_Server), "httpd/%s", hv_compile_version());
    }
    resp->headers["Server"] = s_Server;

    // Connection:
    bool keepalive = handler->keepalive;
    resp->headers["Connection"] = keepalive ? "keep-alive" : "close";

    // Upgrade:
    bool upgrade = false;
    HttpHandler::ProtocolType upgrade_protocol = HttpHandler::UNKNOWN;
    auto iter_upgrade = req->headers.find("upgrade");
    if (iter_upgrade != req->headers.end()) {
        upgrade = true;
        const char* upgrade_proto = iter_upgrade->second.c_str();
        hlogi("[%s:%d] Upgrade: %s", handler->ip, handler->port, upgrade_proto);
        // websocket
        if (stricmp(upgrade_proto, "websocket") == 0) {
            /*
            HTTP/1.1 101 Switching Protocols
            Connection: Upgrade
            Upgrade: websocket
            Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=
            */
            resp->status_code = HTTP_STATUS_SWITCHING_PROTOCOLS;
            resp->headers["Connection"] = "Upgrade";
            resp->headers["Upgrade"] = "websocket";
            auto iter_key = req->headers.find(SEC_WEBSOCKET_KEY);
            if (iter_key != req->headers.end()) {
                char ws_accept[32] = {0};
                ws_encode_key(iter_key->second.c_str(), ws_accept);
                resp->headers[SEC_WEBSOCKET_ACCEPT] = ws_accept;
            }
            upgrade_protocol = HttpHandler::WEBSOCKET;
            // NOTE: SwitchWebSocket after send handshake response
        }
        // h2/h2c
        else if (strnicmp(upgrade_proto, "h2", 2) == 0) {
            /*
            HTTP/1.1 101 Switching Protocols
            Connection: Upgrade
            Upgrade: h2c
            */
            hio_write(io, HTTP2_UPGRADE_RESPONSE, strlen(HTTP2_UPGRADE_RESPONSE));
            if (!handler->SwitchHTTP2()) {
                hloge("[%s:%d] unsupported HTTP2", handler->ip, handler->port);
                hio_close(io);
                return;
            }
            parser = handler->parser.get();
        }
        else {
            hio_close(io);
            return;
        }
    }

    int status_code = 200;
    if (parser->IsComplete() && !upgrade) {
        status_code = handler->HandleHttpRequest();
    }

    char* data = NULL;
    size_t len = 0;
    while (handler->GetSendData(&data, &len)) {
        // printf("%.*s\n", (int)len, data);
        if (data && len) {
            hio_write(io, data, len);
        }
    }

    // access log
    hlogi("[%ld-%ld][%s:%d][%s %s]=>[%d %s]",
        hloop_pid(loop), hloop_tid(loop),
        handler->ip, handler->port,
        http_method_str(req->method), req->path.c_str(),
        resp->status_code, resp->status_message());

    // switch protocol to websocket
    if (upgrade && upgrade_protocol == HttpHandler::WEBSOCKET) {
        if (!handler->SwitchWebSocket(io)) {
            hloge("[%s:%d] unsupported websocket", handler->ip, handler->port);
            hio_close(io);
            return;
        }
        // onopen
        handler->WebSocketOnOpen();
        return;
    }

    if (status_code && !keepalive) {
        hio_close(io);
    }
}

static void on_close(hio_t* io) {
    HttpHandler* handler = (HttpHandler*)hevent_userdata(io);
    if (handler == NULL) return;

    // close proxy
    if (handler->proxy) {
        hio_close_upstream(io);
    }

    // onclose
    if (handler->protocol == HttpHandler::WEBSOCKET) {
        handler->WebSocketOnClose();
    } else {
        if (handler->writer && handler->writer->onclose) {
            handler->writer->onclose();
        }
    }

    EventLoop* loop = currentThreadEventLoop;
    if (loop) {
        --loop->connectionNum;
    }

    hevent_set_userdata(io, NULL);
    delete handler;
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
    HttpHandler* handler = new HttpHandler;
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

    EventLoopPtr loop(new EventLoop);
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

int http_server_run(http_server_t* server, int wait) {
    // http_port
    if (server->port > 0) {
        server->listenfd[0] = Listen(server->port, server->host);
        if (server->listenfd[0] < 0) return server->listenfd[0];
        hlogi("http server listening on %s:%d", server->host, server->port);
    }
    // https_port
    if (server->https_port > 0 && hssl_ctx_instance() != NULL) {
#ifdef WITH_NGHTTP2
#ifdef WITH_OPENSSL
        static unsigned char s_alpn_protos[] = "\x02h2\x08http/1.1\x08http/1.0\x08http/0.9";
        hssl_ctx_t ssl_ctx = hssl_ctx_instance();
        hssl_ctx_set_alpn_protos(ssl_ctx, s_alpn_protos, sizeof(s_alpn_protos) - 1);
#endif
#endif
        server->listenfd[1] = Listen(server->https_port, server->host);
        if (server->listenfd[1] < 0) return server->listenfd[1];
        hlogi("https server listening on %s:%d", server->host, server->https_port);
    }

    HttpServerPrivdata* privdata = new HttpServerPrivdata;
    server->privdata = privdata;
    if (server->service == NULL) {
        privdata->service.reset(new HttpService);
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

    delete privdata;
    server->privdata = NULL;
    return 0;
}
