#include "HttpServer.h"

#include "hv.h"
#include "hmain.h"
#include "hloop.h"

#include "http2def.h"
#include "FileCache.h"
#include "HttpHandler.h"
#include "Http2Parser.h"

#define MIN_HTTP_REQUEST        "GET / HTTP/1.1\r\n\r\n"
#define MIN_HTTP_REQUEST_LEN    14 // exclude CRLF

static HttpService  s_default_service;
static FileCache    s_filecache;

struct HttpServerPrivdata {
    std::vector<hloop_t*>   loops;
    std::mutex              loops_mutex;
};

static void on_recv(hio_t* io, void* _buf, int readbytes) {
    // printf("on_recv fd=%d readbytes=%d\n", hio_fd(io), readbytes);
    const char* buf = (const char*)_buf;
    HttpHandler* handler = (HttpHandler*)hevent_userdata(io);
    // HTTP1 / HTTP2 -> HttpParser -> InitRequest
    // recv -> FeedRecvData -> !WantRecv -> HttpRequest ->
    // HandleRequest -> HttpResponse -> SubmitResponse -> while (GetSendData) -> send
    if (handler->parser == NULL) {
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
        http_version version = HTTP_V1;
        if (strncmp((char*)buf, HTTP2_MAGIC, MIN(readbytes, HTTP2_MAGIC_LEN)) == 0) {
            version = HTTP_V2;
            handler->req.http_major = 2;
            handler->req.http_minor = 0;
        }
        handler->parser = HttpParser::New(HTTP_SERVER, version);
        if (handler->parser == NULL) {
            hloge("[%s:%d] unsupported HTTP%d", handler->ip, handler->port, (int)version);
            hio_close(io);
            return;
        }
        handler->parser->InitRequest(&handler->req);
    }

    HttpParser* parser = handler->parser;
    HttpRequest* req = &handler->req;
    HttpResponse* res = &handler->res;

    int nfeed = parser->FeedRecvData((const char*)buf, readbytes);
    if (nfeed != readbytes) {
        hloge("[%s:%d] http parse error: %s", handler->ip, handler->port, parser->StrError(parser->GetError()));
        hio_close(io);
        return;
    }

    if (parser->WantRecv()) {
        return;
    }

#ifdef WITH_NGHTTP2
    if (parser->version == HTTP_V2) {
        // HTTP2 extra processing steps
        Http2Parser* h2p = (Http2Parser*)parser;
        if (h2p->state == HSS_RECV_PING) {
            char* data = NULL;
            size_t len = 0;
            while (parser->GetSendData(&data, &len)) {
                hio_write(io, data, len);
            }
            return;
        }
        else if ((h2p->state == HSS_RECV_HEADERS && req->method != HTTP_POST) || h2p->state == HSS_RECV_DATA) {
            goto handle_request;
        }
        else {
            // ignore other http2 frame
            return;
        }
    }

    // Upgrade: h2
    {
        auto iter_upgrade = req->headers.find("upgrade");
        if (iter_upgrade != req->headers.end()) {
            hlogi("[%s:%d] Upgrade: %s", handler->ip, handler->port, iter_upgrade->second.c_str());
            // h2/h2c
            if (strnicmp(iter_upgrade->second.c_str(), "h2", 2) == 0) {
                hio_write(io, HTTP2_UPGRADE_RESPONSE, strlen(HTTP2_UPGRADE_RESPONSE));
                SAFE_DELETE(handler->parser);
                parser = handler->parser = HttpParser::New(HTTP_SERVER, HTTP_V2);
                if (parser == NULL) {
                    hloge("[%s:%d] unsupported HTTP2", handler->ip, handler->port);
                    hio_close(io);
                    return;
                }
                HttpRequest http1_req = *req;
                parser->InitRequest(req);
                *req = http1_req;
                req->http_major = 2;
                req->http_minor = 0;
                // HTTP2_Settings: ignore
                // parser->FeedRecvData(HTTP2_Settings, );
            }
            else {
                hio_close(io);
                return;
            }
        }
    }
#endif

handle_request:
    handler->HandleRequest();
    // prepare headers body
    // Server:
    static char s_Server[64] = {'\0'};
    if (s_Server[0] == '\0') {
        snprintf(s_Server, sizeof(s_Server), "httpd/%s", hv_compile_version());
    }
    res->headers["Server"] = s_Server;
    // Connection:
    bool keepalive = true;
    auto iter_keepalive = req->headers.find("connection");
    if (iter_keepalive != req->headers.end()) {
        if (stricmp(iter_keepalive->second.c_str(), "keep-alive") == 0) {
            keepalive = true;
        }
        else if (stricmp(iter_keepalive->second.c_str(), "close") == 0) {
            keepalive = false;
        }
    }
    if (keepalive) {
        res->headers["Connection"] = "keep-alive";
    }
    else {
        res->headers["Connection"] = "close";
    }

    if (req->http_major == 1) {
        std::string header = res->Dump(true, false);
        hbuf_t sendbuf;
        bool send_in_one_packet = true;
        int content_length = res->ContentLength();
        if (handler->fc) {
            // no copy filebuf, more efficient
            handler->fc->prepend_header(header.c_str(), header.size());
            sendbuf = handler->fc->httpbuf;
        }
        else {
            if (content_length > (1 << 20)) {
                send_in_one_packet = false;
            }
            else if (content_length != 0) {
                header.insert(header.size(), (const char*)res->Content(), content_length);
            }
            sendbuf.base = (char*)header.c_str();
            sendbuf.len = header.size();
        }
        // send header/body
        hio_write(io, sendbuf.base, sendbuf.len);
        if (send_in_one_packet == false) {
            // send body
            hio_write(io, res->Content(), content_length);
        }
    }
    else if (req->http_major == 2) {
        parser->SubmitResponse(res);
        char* data = NULL;
        size_t len = 0;
        while (parser->GetSendData(&data, &len)) {
            hio_write(io, data, len);
        }
    }

    static long s_pid = hv_getpid();
    long tid = hv_gettid();
    hlogi("[%ld-%ld][%s:%d][%s %s]=>[%d %s]",
        s_pid, tid,
        handler->ip, handler->port,
        http_method_str(req->method), req->path.c_str(),
        res->status_code, http_status_str(res->status_code));

    if (keepalive) {
        handler->Reset();
        parser->InitRequest(req);
    }
    else {
        hio_close(io);
    }
}

static void on_close(hio_t* io) {
    HttpHandler* handler = (HttpHandler*)hevent_userdata(io);
    if (handler) {
        SAFE_DELETE(handler->parser);
        delete handler;
        hevent_set_userdata(io, NULL);
    }
}

static void on_accept(hio_t* io) {
    printd("on_accept connfd=%d\n", hio_fd(io));
    /*
    char localaddrstr[SOCKADDR_STRLEN] = {0};
    char peeraddrstr[SOCKADDR_STRLEN] = {0};
    printf("accept connfd=%d [%s] <= [%s]\n", hio_fd(io),
            SOCKADDR_STR(hio_localaddr(io), localaddrstr),
            SOCKADDR_STR(hio_peeraddr(io), peeraddrstr));
    */

    hio_setcb_close(io, on_close);
    hio_setcb_read(io, on_recv);
    hio_read(io);
    hio_set_keepalive_timeout(io, HIO_DEFAULT_KEEPALIVE_TIMEOUT);
    // new HttpHandler
    // delete on_close
    HttpHandler* handler = new HttpHandler;
    handler->service = (HttpService*)hevent_userdata(io);
    handler->files = &s_filecache;
    sockaddr_ip((sockaddr_u*)hio_peeraddr(io), handler->ip, sizeof(handler->ip));
    handler->port = sockaddr_port((sockaddr_u*)hio_peeraddr(io));
    hevent_set_userdata(io, handler);
}

static void handle_cached_files(htimer_t* timer) {
    FileCache* pfc = (FileCache*)hevent_userdata(timer);
    if (pfc == NULL) {
        htimer_del(timer);
        return;
    }
    file_cache_t* fc = NULL;
    time_t tt;
    time(&tt);
    std::lock_guard<std::mutex> locker(pfc->mutex_);
    auto iter = pfc->cached_files.begin();
    while (iter != pfc->cached_files.end()) {
        fc = iter->second;
        if (tt - fc->stat_time > pfc->file_cached_time) {
            delete fc;
            iter = pfc->cached_files.erase(iter);
            continue;
        }
        ++iter;
    }
}

static void fsync_logfile(hidle_t* idle) {
    hlog_fsync();
}

static void worker_fn(void* userdata) {
    http_server_t* server = (http_server_t*)userdata;
    int listenfd = server->listenfd;
    hloop_t* loop = hloop_new(0);
    hio_t* listenio = haccept(loop, listenfd, on_accept);
    hevent_set_userdata(listenio, server->service);
    if (server->ssl) {
        hio_enable_ssl(listenio);
    }
    // fsync logfile when idle
    hlog_disable_fsync();
    hidle_add(loop, fsync_logfile, INFINITE);
    // timer handle_cached_files
    htimer_t* timer = htimer_add(loop, handle_cached_files, s_filecache.file_cached_time * 1000);
    hevent_set_userdata(timer, &s_filecache);
    // for SDK implement http_server_stop
    HttpServerPrivdata* privdata = (HttpServerPrivdata*)server->privdata;
    privdata->loops_mutex.lock();
    privdata->loops.push_back(loop);
    privdata->loops_mutex.unlock();
    hloop_run(loop);
    hloop_free(&loop);
}

int http_server_run(http_server_t* server, int wait) {
    // service
    if (server->service == NULL) {
        server->service = &s_default_service;
    }
    // port
    server->listenfd = Listen(server->port, server->host);
    if (server->listenfd < 0) return server->listenfd;

    // privdata
    server->privdata = new HttpServerPrivdata;

    if (server->worker_processes) {
        return master_workers_run(worker_fn, server, server->worker_processes, server->worker_threads, wait);
    }
    else {
        // NOTE: master_workers_run use global-vars that may be used by other,
        // so we implement Multi-Threads directly.
        int worker_threads = server->worker_threads;
        if (worker_threads == 0) worker_threads = 1;
        if (wait) {
            for (int i = 1; i < worker_threads; ++i) {
                hthread_create((hthread_routine)worker_fn, server);
            }
            worker_fn(server);
        }
        else {
            for (int i = 0; i < worker_threads; ++i) {
                hthread_create((hthread_routine)worker_fn, server);
            }
        }
        return 0;
    }
}

int http_server_stop(http_server_t* server) {
    HttpServerPrivdata* privdata = (HttpServerPrivdata*)server->privdata;
    for (auto& loop : privdata->loops) {
        hloop_stop(loop);
    }
    SAFE_DELETE(privdata);
    return 0;
}
