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

static HttpService* default_http_service() {
    static HttpService* s_default_service = new HttpService;
    return s_default_service;
}

static FileCache* default_filecache() {
    static FileCache s_filecache;
    return &s_filecache;
}

struct HttpServerPrivdata {
    std::vector<EventLoopPtr>   loops;
    std::vector<hthread_t>      threads;
    std::mutex                  mutex_;
};

static void websocket_heartbeat(hio_t* io) {
    HttpHandler* handler = (HttpHandler*)hevent_userdata(io);
    WebSocketHandler* ws = handler->ws.get();
    if (ws->last_recv_pong_time < ws->last_send_ping_time) {
        hlogw("[%s:%d] websocket no pong!", handler->ip, handler->port);
        hio_close(io);
    } else {
        // printf("send ping\n");
        hio_write(io, WS_SERVER_PING_FRAME, WS_SERVER_MIN_FRAME_SIZE);
        ws->last_send_ping_time = gethrtime_us();
    }
}

static void websocket_onmessage(int opcode, const std::string& msg, hio_t* io) {
    HttpHandler* handler = (HttpHandler*)hevent_userdata(io);
    WebSocketHandler* ws = handler->ws.get();
    switch(opcode) {
    case WS_OPCODE_CLOSE:
        hio_close_async(io);
        break;
    case WS_OPCODE_PING:
        // printf("recv ping\n");
        // printf("send pong\n");
        hio_write(io, WS_SERVER_PONG_FRAME, WS_SERVER_MIN_FRAME_SIZE);
        break;
    case WS_OPCODE_PONG:
        // printf("recv pong\n");
        ws->last_recv_pong_time = gethrtime_us();
        break;
    case WS_OPCODE_TEXT:
    case WS_OPCODE_BINARY:
        // onmessage
        handler->WebSocketOnMessage(msg);
        break;
    default:
        break;
    }
}

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
        int http_version = 1;
        if (strncmp((char*)buf, HTTP2_MAGIC, MIN(readbytes, HTTP2_MAGIC_LEN)) == 0) {
            http_version = 2;
        }
        if (!handler->Init(http_version)) {
            hloge("[%s:%d] unsupported HTTP%d", handler->ip, handler->port, http_version);
            hio_close(io);
            return;
        }
        handler->writer.reset(new HttpResponseWriter(io, handler->resp));
        if (handler->writer) {
            handler->writer->status = SocketChannel::CONNECTED;
        }
    }

    int nfeed = handler->FeedRecvData(buf, readbytes);
    if (nfeed != readbytes) {
        hio_close(io);
        return;
    }

    if (protocol == HttpHandler::WEBSOCKET) {
        return;
    }

    HttpParser* parser = handler->parser.get();
    if (parser->WantRecv()) {
        return;
    }

    HttpRequest* req = handler->req.get();
    HttpResponse* resp = handler->resp.get();

    // Server:
    static char s_Server[64] = {'\0'};
    if (s_Server[0] == '\0') {
        snprintf(s_Server, sizeof(s_Server), "httpd/%s", hv_compile_version());
    }
    resp->headers["Server"] = s_Server;

    // Connection:
    bool keepalive = req->IsKeepAlive();
    if (keepalive) {
        resp->headers["Connection"] = "keep-alive";
    } else {
        resp->headers["Connection"] = "close";
    }

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

    // LOG
    hloop_t* loop = hevent_loop(io);
    hlogi("[%ld-%ld][%s:%d][%s %s]=>[%d %s]",
        hloop_pid(loop), hloop_tid(loop),
        handler->ip, handler->port,
        http_method_str(req->method), req->path.c_str(),
        resp->status_code, resp->status_message());

    // switch protocol to websocket
    if (upgrade && upgrade_protocol == HttpHandler::WEBSOCKET) {
        WebSocketHandler* ws = handler->SwitchWebSocket();
        ws->channel.reset(new WebSocketChannel(io, WS_SERVER));
        ws->parser->onMessage = std::bind(websocket_onmessage, std::placeholders::_1, std::placeholders::_2, io);
        // NOTE: cancel keepalive timer, judge alive by heartbeat.
        hio_set_keepalive_timeout(io, 0);
        if (handler->ws_service && handler->ws_service->ping_interval > 0) {
            int ping_interval = MAX(handler->ws_service->ping_interval, 1000);
            hio_set_heartbeat(io, ping_interval, websocket_heartbeat);
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
    if (handler) {
        if (handler->protocol == HttpHandler::WEBSOCKET) {
            // onclose
            handler->WebSocketOnClose();
        }
        if (handler->writer) {
            handler->writer->status = SocketChannel::DISCONNECTED;
        }
        hevent_set_userdata(io, NULL);
        delete handler;
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

    hio_setcb_close(io, on_close);
    hio_setcb_read(io, on_recv);
    hio_read(io);
    hio_set_keepalive_timeout(io, service->keepalive_timeout);

    // new HttpHandler, delete on_close
    HttpHandler* handler = new HttpHandler;
    // ssl
    handler->ssl = hio_is_ssl(io);
    // ip
    sockaddr_ip((sockaddr_u*)hio_peeraddr(io), handler->ip, sizeof(handler->ip));
    // port
    handler->port = sockaddr_port((sockaddr_u*)hio_peeraddr(io));
    // service
    handler->service = service;
    // ws
    handler->ws_service = server->ws;
    // FileCache
    handler->files = default_filecache();
    hevent_set_userdata(io, handler);
}

static void loop_thread(void* userdata) {
    http_server_t* server = (http_server_t*)userdata;

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
        // NOTE: add timer to remove expired file cache
        htimer_add(hloop, [](htimer_t*) {
            FileCache* filecache = default_filecache();
            filecache->RemoveExpiredFileCache();
        }, DEFAULT_FILE_EXPIRED_TIME * 1000);
        // NOTE: add timer to update date every 1s
        htimer_add(hloop, [](htimer_t* timer) {
            gmtime_fmt(hloop_now(hevent_loop(timer)), HttpMessage::s_date);
        }, 1000);
    }
    privdata->loops.push_back(loop);
    privdata->mutex_.unlock();

    loop->run();
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
        server->listenfd[1] = Listen(server->https_port, server->host);
        if (server->listenfd[1] < 0) return server->listenfd[1];
        hlogi("https server listening on %s:%d", server->host, server->https_port);
    }
    // service
    if (server->service == NULL) {
        server->service = default_http_service();
    }

    HttpServerPrivdata* privdata = new HttpServerPrivdata;
    server->privdata = privdata;

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
#ifdef OS_UNIX
    if (server->worker_processes) {
        signal_handle("stop");
        return 0;
    }
#endif

    HttpServerPrivdata* privdata = (HttpServerPrivdata*)server->privdata;
    if (privdata == NULL) return 0;

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
