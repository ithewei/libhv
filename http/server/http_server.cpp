#include "http_server.h"

#include "h.h"
#include "hmain.h"
#include "hloop.h"
#include "hbuf.h"

#include "FileCache.h"
#include "HttpParser.h"
#include "HttpHandler.h"

#define RECV_BUFSIZE    8192
#define SEND_BUFSIZE    8192

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

static void on_recv(hio_t* io, void* buf, int readbytes) {
    //printf("on_recv fd=%d readbytes=%d\n", io->fd, readbytes);
    HttpHandler* handler = (HttpHandler*)io->userdata;
    HttpParser* parser = &handler->parser;
    // recv -> HttpParser -> HttpRequest -> handle_request -> HttpResponse -> send
    int nparse = parser->execute((char*)buf, readbytes);
    if (nparse != readbytes || parser->get_errno() != HPE_OK) {
        hloge("[%s:%d] http parser error: %s", handler->srcip, handler->srcport, http_errno_description(parser->get_errno()));
        hclose(io);
        return;
    }
    if (parser->get_state() == HP_MESSAGE_COMPLETE) {
        handler->handle_request();
        // prepare header body
        // Server:
        static char s_Server[64] = {'\0'};
        if (s_Server[0] == '\0') {
            snprintf(s_Server, sizeof(s_Server), "httpd/%s", get_compile_version());
        }
        handler->res.headers["Server"] = s_Server;
        // Connection:
        bool keepalive = true;
        auto iter = handler->req.headers.find("connection");
        if (iter != handler->req.headers.end()) {
            if (stricmp(iter->second.c_str(), "keep-alive") == 0) {
                keepalive = true;
            }
            else if (stricmp(iter->second.c_str(), "close") == 0) {
                keepalive = false;
            }
        }
        if (keepalive) {
            handler->res.headers["Connection"] = "keep-alive";
        }
        else {
            handler->res.headers["Connection"] = "close";
        }
        std::string header = handler->res.dump(true, false);
        hbuf_t sendbuf;
        bool send_in_one_packet = true;
        if (handler->fc) {
            handler->fc->prepend_header(header.c_str(), header.size());
            sendbuf = handler->fc->httpbuf;
        }
        else {
            if (handler->res.body.size() > (1<<20)) {
                send_in_one_packet = false;
            } else if (handler->res.body.size() != 0) {
                header += handler->res.body;
            }
            sendbuf.base = (char*)header.c_str();
            sendbuf.len = header.size();
        }
        // send header/body
        hsend(io->loop, io->fd, sendbuf.base, sendbuf.len);
        if (send_in_one_packet == false) {
            // send body
            hsend(io->loop, io->fd, handler->res.body.data(), handler->res.body.size());
        }

        hlogi("[%s:%d][%s %s]=>[%d %s]",
            handler->srcip, handler->srcport,
            http_method_str(handler->req.method), handler->req.url.c_str(),
            handler->res.status_code, http_status_str(handler->res.status_code));

        if (keepalive) {
            handler->reset();
            handler->keepalive();
        }
        else {
            hclose(io);
        }
    }
}

static void on_close(hio_t* io) {
    HttpHandler* handler = (HttpHandler*)io->userdata;
    if (handler) {
        delete handler;
        io->userdata = NULL;
    }
}

static void on_accept(hio_t* io, int connfd) {
    //printf("on_accept listenfd=%d connfd=%d\n", io->fd, connfd);
    /*
    char localaddrstr[INET6_ADDRSTRLEN+16] = {0};
    char peeraddrstr[INET6_ADDRSTRLEN+16] = {0};
    printf("accept listenfd=%d connfd=%d [%s] <= [%s]\n", io->fd, connfd,
            sockaddr_snprintf(io->localaddr, localaddrstr, sizeof(localaddrstr)),
            sockaddr_snprintf(io->peeraddr, peeraddrstr, sizeof(peeraddrstr)));
    */

    HBuf* buf = (HBuf*)io->loop->userdata;
    hio_t* connio = hrecv(io->loop, connfd, buf->base, buf->len, on_recv);
    connio->close_cb = on_close;
    // new HttpHandler
    // delete on_close
    HttpHandler* handler = new HttpHandler;
    handler->service = (HttpService*)io->userdata;
    handler->files = &s_filecache;
    sockaddr_ntop(io->peeraddr, handler->srcip, sizeof(handler->srcip));
    handler->srcport = sockaddr_htons(io->peeraddr);
    handler->io = connio;
    connio->userdata = handler;
}

static void handle_cached_files(htimer_t* timer) {
    FileCache* pfc = (FileCache*)timer->userdata;
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

static void fflush_log(hidle_t* idle) {
    hlog_fflush();
}

// for implement http_server_stop
static hloop_t* s_loop = NULL;

static void worker_proc(void* userdata) {
    http_server_t* server = (http_server_t*)userdata;
    int listenfd = server->listenfd;
    hloop_t loop;
    s_loop = &loop;
    hloop_init(&loop);
    // one loop one readbuf.
    HBuf readbuf;
    readbuf.resize(RECV_BUFSIZE);
    loop.userdata = &readbuf;
    hio_t* listenio = haccept(&loop, listenfd, on_accept);
    listenio->userdata = server->service;
    // fflush logfile when idle
    hlog_set_fflush(0);
    hidle_add(&loop, fflush_log, INFINITE);
    // timer handle_cached_files
    htimer_t* timer = htimer_add(&loop, handle_cached_files, s_filecache.file_cached_time*1000);
    timer->userdata = &s_filecache;
    hloop_run(&loop);
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
    server->listenfd = Listen(server->port);
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

// for SDK, just use for singleton
int http_server_stop(http_server_t* server) {
    if (s_loop) {
        hloop_stop(s_loop);
        s_loop = NULL;
    }
    return 0;
}
