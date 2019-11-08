#include "HttpServer.h"

#include "h.h"
#include "hmain.h"
#include "hloop.h"

#include "http2def.h"
#include "FileCache.h"
#include "HttpHandler.h"
#include "Http2Session.h"

#define RECV_BUFSIZE    8192
#define SEND_BUFSIZE    8192
#define MIN_HTTP_REQUEST        "GET / HTTP/1.1\r\n\r\n"
#define MIN_HTTP_REQUEST_LEN    14 // exclude CRLF

static HttpService  s_default_service;
static FileCache    s_filecache;

static void master_init(void* userdata) {
#ifdef OS_UNIX
    char proctitle[256] = {0};
    snprintf(proctitle, sizeof(proctitle), "%s: master process", g_main_ctx.program_name);
    setproctitle(proctitle);
#endif
}

static void master_proc(void* userdata) {
    while(1) sleep(1);
}

static void worker_init(void* userdata) {
#ifdef OS_UNIX
    char proctitle[256] = {0};
    snprintf(proctitle, sizeof(proctitle), "%s: worker process", g_main_ctx.program_name);
    setproctitle(proctitle);
    signal(SIGNAL_RELOAD, signal_handler);
#endif
}

static void on_recv(hio_t* io, void* _buf, int readbytes) {
    //printf("on_recv fd=%d readbytes=%d\n", hio_fd(io), readbytes);
    const char* buf = (const char*)_buf;
    HttpHandler* handler = (HttpHandler*)hevent_userdata(io);
    // HTTP1 / HTTP2 -> HttpSession -> InitRequest
    // recv -> FeedRecvData -> !WantRecv -> HttpRequest ->
    // HandleRequest -> HttpResponse -> SubmitResponse -> while (GetSendData) -> send
    if (handler->session == NULL) {
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
        handler->session = HttpSession::New(HTTP_SERVER, version);
        if (handler->session == NULL) {
            hloge("[%s:%d] unsupported HTTP%d", handler->ip, handler->port, (int)version);
            hio_close(io);
            return;
        }
        handler->session->InitRequest(&handler->req);
    }

    HttpSession* session = handler->session;
    HttpRequest* req = &handler->req;
    HttpResponse* res = &handler->res;

    int nfeed = session->FeedRecvData((const char*)buf, readbytes);
    if (nfeed != readbytes) {
        hloge("[%s:%d] http parse error: %s", handler->ip, handler->port, session->StrError(session->GetError()));
        hio_close(io);
        return;
    }

    if (session->WantRecv()) {
        // NOTE: KeepAlive will reset keepalive_timer,
        // if no data recv within keepalive timeout, closesocket actively.
        handler->KeepAlive();
        return;
    }

#ifdef WITH_NGHTTP2
    if (session->version == HTTP_V2) {
        // HTTP2 extra processing steps
        Http2Session* h2s = (Http2Session*)session;
        if (h2s->state == HSS_RECV_PING) {
            char* data = NULL;
            size_t len = 0;
            while (session->GetSendData(&data, &len)) {
                hio_write(io, data, len);
            }
            return;
        }
        else if ((h2s->state == HSS_RECV_HEADERS && req->method != HTTP_POST) || h2s->state == HSS_RECV_DATA) {
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
            SAFE_DELETE(handler->session);
            session = handler->session = HttpSession::New(HTTP_SERVER, HTTP_V2);
            if (session == NULL) {
                hloge("[%s:%d] unsupported HTTP2", handler->ip, handler->port);
                hio_close(io);
                return;
            }
            HttpRequest http1_req = *req;
            session->InitRequest(req);
            *req = http1_req;
            req->http_major = 2;
            req->http_minor = 0;
            // HTTP2_Settings: ignore
            //session->FeedRecvData(HTTP2_Settings, );
        }
        else {
            hio_close(io);
            return;
        }
    }
    }
#endif

handle_request:
    int ret = handler->HandleRequest();
    // prepare headers body
    // Server:
    static char s_Server[64] = {'\0'};
    if (s_Server[0] == '\0') {
        snprintf(s_Server, sizeof(s_Server), "httpd/%s", get_compile_version());
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
            if (content_length > (1<<20)) {
                send_in_one_packet = false;
            } else if (content_length != 0) {
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
        session->SubmitResponse(res);
        char* data = NULL;
        size_t len = 0;
        while (session->GetSendData(&data, &len)) {
            hio_write(io, data, len);
        }
    }

    hlogi("[%s:%d][%s %s]=>[%d %s]",
        handler->ip, handler->port,
        http_method_str(req->method), req->path.c_str(),
        res->status_code, http_status_str(res->status_code));

    if (keepalive) {
        handler->KeepAlive();
        handler->Reset();
        session->InitRequest(req);
    }
    else {
        hio_close(io);
    }
}

static void on_close(hio_t* io) {
    HttpHandler* handler = (HttpHandler*)hevent_userdata(io);
    if (handler) {
        SAFE_DELETE(handler->session);
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

    HBuf* buf = (HBuf*)hloop_userdata(hevent_loop(io));
    hio_setcb_close(io, on_close);
    hio_setcb_read(io, on_recv);
    hio_set_readbuf(io, buf->base, buf->len);
    hio_read(io);
    // new HttpHandler
    // delete on_close
    HttpHandler* handler = new HttpHandler;
    handler->service = (HttpService*)hevent_userdata(io);
    handler->files = &s_filecache;
    sockaddr_ip((sockaddr_un*)hio_peeraddr(io), handler->ip, sizeof(handler->ip));
    handler->port = sockaddr_port((sockaddr_un*)hio_peeraddr(io));
    handler->io = io;
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

static void worker_proc(void* userdata) {
    http_server_t* server = (http_server_t*)userdata;
    int listenfd = server->listenfd;
    hloop_t* loop = hloop_new(0);
    // for SDK implement http_server_stop
    if (server->worker_processes == 0) {
        server->privdata = (void*)loop;
    }
    // one loop one readbuf.
    HBuf readbuf;
    readbuf.resize(RECV_BUFSIZE);
    hloop_set_userdata(loop, &readbuf);
    hio_t* listenio = haccept(loop, listenfd, on_accept);
    hevent_set_userdata(listenio, server->service);
    if (server->ssl) {
        hio_enable_ssl(listenio);
    }
    // fsync logfile when idle
    hlog_disable_fsync();
    hidle_add(loop, fsync_logfile, INFINITE);
    // timer handle_cached_files
    htimer_t* timer = htimer_add(loop, handle_cached_files, s_filecache.file_cached_time*1000);
    hevent_set_userdata(timer, &s_filecache);
    hloop_run(loop);
    hloop_free(&loop);
}

int http_server_run(http_server_t* server, int wait) {
    // worker_processes
    if (server->worker_processes != 0 && g_worker_processes_num != 0 && g_worker_processes != NULL) {
        return ERR_OVER_LIMIT;
    }
    // service
    if (server->service == NULL) {
        server->service = &s_default_service;
    }
    // port
    server->listenfd = Listen(server->port, server->host);
    if (server->listenfd < 0) return server->listenfd;

#ifdef OS_WIN
    if (server->worker_processes > 1) {
        server->worker_processes = 1;
    }
#endif

    if (server->worker_processes == 0) {
        worker_proc(server);
    }
    else {
        // master-workers processes
        g_worker_processes_num = server->worker_processes;
        int bytes = g_worker_processes_num * sizeof(proc_ctx_t);
        g_worker_processes = (proc_ctx_t*)malloc(bytes);
        memset(g_worker_processes, 0, bytes);
        for (int i = 0; i < g_worker_processes_num; ++i) {
            proc_ctx_t* ctx = g_worker_processes + i;
            ctx->init = worker_init;
            ctx->init_userdata = NULL;
            ctx->proc = worker_proc;
            ctx->proc_userdata = server;
            spawn_proc(ctx);
        }
        if (wait) {
            master_init(NULL);
            master_proc(NULL);
        }
    }

    return 0;
}

int http_server_stop(http_server_t* server) {
    if (server->worker_processes == 0) {
        if (server->privdata) {
            hloop_t* loop = (hloop_t*)server->privdata;
            hloop_stop(loop);
            server->privdata = NULL;
        }
    }
    return 0;
}
