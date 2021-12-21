/*
 * tinyproxyd       tiny http proxy server
 *
 * @build           make examples
 *
 * @http_server     bin/tinyhttpd  8000
 * @proxy_server    bin/tinyproxyd 1080
 *
 * @proxy_client    bin/curl -v www.httpbin.org/get --http-proxy 127.0.0.1:1080
 *                  bin/curl -v www.httpbin.org/post -d hello --http-proxy 127.0.0.1:1080
 *                      curl -v www.httpbin.org/get --proxy http://127.0.0.1:1080
 *                      curl -v www.httpbin.org/post -d hello --proxy http://127.0.0.1:1080
 *
 */

#include "hv.h"
#include "hloop.h"

/*
 * workflow:
 * hloop_new -> hloop_create_tcp_server -> hloop_run ->
 * on_accept -> HV_ALLOC(http_conn_t) -> hio_readline ->
 * on_recv -> parse_http_request_line -> hio_readline ->
 * on_recv -> parse_http_head -> ...  -> hio_readline ->
 * on_head_end -> hio_setup_upstream ->
 * on_upstream_connect -> hio_write_upstream(head) ->
 * on_body -> hio_write_upstream(body) ->
 * on_upstream_close -> hio_close ->
 * on_close -> HV_FREE(http_conn_t)
 *
 */

static char proxy_host[64] = "0.0.0.0";
static int  proxy_port = 1080;
static int  proxy_ssl = 0;

static int thread_num = 1;
static hloop_t*  accept_loop = NULL;
static hloop_t** worker_loops = NULL;

#define HTTP_KEEPALIVE_TIMEOUT  60000 // ms
#define HTTP_MAX_URL_LENGTH     256
#define HTTP_MAX_HEAD_LENGTH    1024

typedef enum {
    s_begin,
    s_first_line,
    s_request_line = s_first_line,
    s_status_line = s_first_line,
    s_head,
    s_head_end,
    s_body,
    s_end
} http_state_e;

typedef struct {
    // first line
    int             major_version;
    int             minor_version;
    union {
        // request line
        struct {
            char method[32];
            char path[HTTP_MAX_URL_LENGTH];
        };
        // status line
        struct {
            int  status_code;
            char status_message[64];
        };
    };
    // headers
    char        host[64];
    int         content_length;
    char        content_type[64];
    unsigned    keepalive:  1;
    unsigned    proxy:      1;
    char        head[HTTP_MAX_HEAD_LENGTH];
    int         head_len;
    // body
    char*       body;
    int         body_len; // body_len = content_length
} http_msg_t;

typedef struct {
    hio_t*          io;
    http_state_e    state;
    http_msg_t      request;
//  http_msg_t      response;
} http_conn_t;

static int http_request_dump(http_conn_t* conn, char* buf, int len) {
    http_msg_t* msg = &conn->request;
    int offset = 0;
    // request line
    const char* path = msg->path;
    if (msg->proxy) {
        const char* pos = strstr(msg->path, "://");
        pos = pos ? pos + 3 : msg->path;
        path = strchr(pos, '/');
    }
    if (path == NULL) path = "/";
    offset += snprintf(buf + offset, len - offset, "%s %s HTTP/%d.%d\r\n", msg->method, path, msg->major_version, msg->minor_version);
    // headers
    if (msg->proxy) {
        if (msg->head_len) {
            memcpy(buf + offset, msg->head, msg->head_len);
            offset += msg->head_len;
        }
        char peeraddrstr[SOCKADDR_STRLEN] = {0};
        SOCKADDR_STR(hio_peeraddr(conn->io), peeraddrstr);
        offset += snprintf(buf + offset, len - offset, "X-Origin-IP: %s\r\n", peeraddrstr);
    } else {
        offset += snprintf(buf + offset, len - offset, "Connection: %s\r\n", msg->keepalive ? "keep-alive" : "close");
        if (msg->content_length > 0) {
            offset += snprintf(buf + offset, len - offset, "Content-Length: %d\r\n", msg->content_length);
        }
        if (*msg->content_type) {
            offset += snprintf(buf + offset, len - offset, "Content-Type: %s\r\n", msg->content_type);
        }
    }
    // TODO: Add your headers
    offset += snprintf(buf + offset, len - offset, "\r\n");
    // body
    if (msg->body && msg->content_length > 0) {
        memcpy(buf + offset, msg->body, msg->content_length);
        offset += msg->content_length;
    }
    return offset;
}

static bool parse_http_request_line(http_conn_t* conn, char* buf, int len) {
    // GET / HTTP/1.1
    http_msg_t* req = &conn->request;
    sscanf(buf, "%s %s HTTP/%d.%d", req->method, req->path, &req->major_version, &req->minor_version);
    if (req->major_version != 1) return false;
    if (req->minor_version == 1) req->keepalive = 1;
    // printf("%s %s HTTP/%d.%d\r\n", req->method, req->path, req->major_version, req->minor_version);
    return true;
}

static bool parse_http_head(http_conn_t* conn, char* buf, int len) {
    http_msg_t* req = &conn->request;
    // Content-Type: text/html
    const char* key = buf;
    const char* val = buf;
    char* delim = strchr(buf, ':');
    if (!delim) return false;
    *delim = '\0';
    val = delim + 1;
    // trim space
    while (*val == ' ') ++val;
    // printf("%s: %s\r\n", key, val);
    if (stricmp(key, "Host") == 0) {
        strncpy(req->host, val, sizeof(req->host) - 1);
    } else if (stricmp(key, "Content-Length") == 0) {
        req->content_length = atoi(val);
    } else if (stricmp(key, "Content-Type") == 0) {
        strncpy(req->content_type, val, sizeof(req->content_type) - 1);
    } else if (stricmp(key, "Connection") == 0 || stricmp(key, "Proxy-Connection") == 0) {
        if (stricmp(val, "close") == 0) {
            req->keepalive = 0;
        }
    }
    return true;
}

static void on_upstream_connect(hio_t* upstream_io) {
    // printf("on_upstream_connect\n");
    http_conn_t* conn = (http_conn_t*)hevent_userdata(upstream_io);
    http_msg_t* req = &conn->request;
    // send head
    char stackbuf[HTTP_MAX_HEAD_LENGTH + 1024] = {0};
    char* buf = stackbuf;
    int buflen = sizeof(stackbuf);
    int msglen = http_request_dump(conn, buf, buflen);
    hio_write(upstream_io, buf, msglen);
    if (conn->state != s_end) {
        // start recv body then upstream
        hio_read_start(conn->io);
    } else {
        if (req->keepalive) {
            // Connection: keep-alive\r\n
            // reset and receive next request
            memset(&conn->request,  0, sizeof(http_msg_t));
            // memset(&conn->response, 0, sizeof(http_msg_t));
            conn->state = s_first_line;
            hio_readline(conn->io);
        }
    }
    // start recv response
    hio_read_start(upstream_io);
}

static int on_head_end(http_conn_t* conn) {
    http_msg_t* req = &conn->request;
    if (req->host[0] == '\0') {
        fprintf(stderr, "No Host header!\n");
        return -1;
    }
    char backend_host[64] = {0};
    strcpy(backend_host, req->host);
    int backend_port = 80;
    char* pos = strchr(backend_host, ':');
    if (pos) {
        *pos = '\0';
        backend_port = atoi(pos + 1);
    }
    if (backend_port == proxy_port &&
        (strcmp(backend_host, proxy_host) == 0 ||
         strcmp(backend_host, "localhost") == 0 ||
         strcmp(backend_host, "127.0.0.1") == 0)) {
        req->proxy = 0;
        return 0;
    }
    // NOTE: blew for proxy
    req->proxy = 1;
    int backend_ssl = strncmp(req->path, "https", 5) == 0 ? 1 : 0;
    // printf("upstream %s:%d\n", backend_host, backend_port);
    hloop_t* loop = hevent_loop(conn->io);
    // hio_t* upstream_io = hio_setup_tcp_upstream(conn->io, backend_host, backend_port, backend_ssl);
    hio_t* upstream_io = hio_create_socket(loop, backend_host, backend_port, HIO_TYPE_TCP, HIO_CLIENT_SIDE);
    if (upstream_io == NULL) {
        fprintf(stderr, "Failed to upstream %s:%d!\n", backend_host, backend_port);
        return -3;
    }
    if (backend_ssl) {
        hio_enable_ssl(upstream_io);
    }
    hevent_set_userdata(upstream_io, conn);
    hio_setup_upstream(conn->io, upstream_io);
    hio_setcb_read(upstream_io, hio_write_upstream);
    hio_setcb_close(upstream_io, hio_close_upstream);
    hio_setcb_connect(upstream_io, on_upstream_connect);
    hio_connect(upstream_io);
    return 0;
}

static int on_body(http_conn_t* conn, void* buf, int readbytes) {
    http_msg_t* req = &conn->request;
    if (req->proxy) {
        hio_write_upstream(conn->io, buf, readbytes);
    }
    return 0;
}

static int on_request(http_conn_t* conn) {
    // NOTE: just reply 403, please refer to examples/tinyhttpd if you want to reply other.
    http_msg_t* req = &conn->request;
    char buf[256] = {0};
    int len = snprintf(buf, sizeof(buf), "HTTP/%d.%d %d %s\r\nContent-Length: 0\r\n\r\n",
            req->major_version, req->minor_version, 403, "Forbidden");
    hio_write(conn->io, buf, len);
    return 403;
}

static void on_close(hio_t* io) {
    // printf("on_close fd=%d error=%d\n", hio_fd(io), hio_error(io));
    http_conn_t* conn = (http_conn_t*)hevent_userdata(io);
    if (conn) {
        HV_FREE(conn);
        hevent_set_userdata(io, NULL);
    }
    hio_close_upstream(io);
}

static void on_recv(hio_t* io, void* buf, int readbytes) {
    char* str = (char*)buf;
    // printf("on_recv fd=%d readbytes=%d\n", hio_fd(io), readbytes);
    // printf("%.*s", readbytes, str);
    http_conn_t* conn = (http_conn_t*)hevent_userdata(io);
    http_msg_t* req = &conn->request;
    switch (conn->state) {
    case s_begin:
        // printf("s_begin");
        conn->state = s_first_line;
    case s_first_line:
        // printf("s_first_line\n");
        if (readbytes < 2) {
            fprintf(stderr, "Not match \r\n!");
            hio_close(io);
            return;
        }
        str[readbytes - 2] = '\0';
        if (parse_http_request_line(conn, str, readbytes - 2) == false) {
            fprintf(stderr, "Failed to parse http request line:\n%s\n", str);
            hio_close(io);
            return;
        }
        // start read head
        conn->state = s_head;
        hio_readline(io);
        break;
    case s_head:
        // printf("s_head\n");
        if (readbytes < 2) {
            fprintf(stderr, "Not match \r\n!");
            hio_close(io);
            return;
        }
        if (readbytes == 2 && str[0] == '\r' && str[1] == '\n') {
            conn->state = s_head_end;
        } else {
            // NOTE: save head
            if (strnicmp(str, "Proxy-", 6) != 0) {
                if (req->head_len + readbytes < HTTP_MAX_HEAD_LENGTH) {
                    memcpy(req->head + req->head_len, buf, readbytes);
                    req->head_len += readbytes;
                }
            }
            str[readbytes - 2] = '\0';
            if (parse_http_head(conn, str, readbytes - 2) == false) {
                fprintf(stderr, "Failed to parse http head:\n%s\n", str);
                hio_close(io);
                return;
            }
            hio_readline(io);
            break;
        }
    case s_head_end:
        // printf("s_head_end\n");
        if (on_head_end(conn) < 0) {
            hio_close(io);
            return;
        }
        if (req->content_length == 0) {
            conn->state = s_end;
            if (req->proxy) {
                // NOTE: wait upstream connect!
            } else {
                goto s_end;
            }
        } else {
            conn->state = s_body;
            if (req->proxy) {
                // NOTE: start read body on_upstream_connect
                // hio_read_start(io);
            } else {
                // WARN: too large content_length should read multiple times!
                hio_readbytes(io, req->content_length);
            }
            break;
        }
    case s_body:
        // printf("s_body\n");
        if (on_body(conn, buf, readbytes) < 0) {
            hio_close(io);
            return;
        }
        req->body = str;
        req->body_len += readbytes;
        if (readbytes == req->content_length) {
            conn->state = s_end;
        } else {
            // Not end
            break;
        }
    case s_end:
s_end:
        // printf("s_end\n");
        // received complete request
        if (req->proxy) {
            // NOTE: reply by upstream
        } else {
            on_request(conn);
        }
        if (hio_is_closed(io)) return;
        if (req->keepalive) {
            // Connection: keep-alive\r\n
            // reset and receive next request
            memset(&conn->request,  0, sizeof(http_msg_t));
            // memset(&conn->response, 0, sizeof(http_msg_t));
            conn->state = s_first_line;
            hio_readline(io);
        } else {
            // Connection: close\r\n
            if (req->proxy) {
                // NOTE: wait upstream close!
            } else {
                hio_close(io);
            }
        }
        break;
    default: break;
    }
}

static void new_conn_event(hevent_t* ev) {
    hloop_t* loop = ev->loop;
    hio_t* io = (hio_t*)hevent_userdata(ev);
    hio_attach(loop, io);

    /*
    char localaddrstr[SOCKADDR_STRLEN] = {0};
    char peeraddrstr[SOCKADDR_STRLEN] = {0};
    printf("tid=%ld connfd=%d [%s] <= [%s]\n",
            (long)hv_gettid(),
            (int)hio_fd(io),
            SOCKADDR_STR(hio_localaddr(io), localaddrstr),
            SOCKADDR_STR(hio_peeraddr(io), peeraddrstr));
    */

    hio_setcb_close(io, on_close);
    hio_setcb_read(io, on_recv);
    hio_set_keepalive_timeout(io, HTTP_KEEPALIVE_TIMEOUT);

    http_conn_t* conn = NULL;
    HV_ALLOC_SIZEOF(conn);
    conn->io = io;
    hevent_set_userdata(io, conn);
    // start read first line
    conn->state = s_first_line;
    hio_readline(io);
}

static hloop_t* get_next_loop() {
    static int s_cur_index = 0;
    if (s_cur_index == thread_num) {
        s_cur_index = 0;
    }
    return worker_loops[s_cur_index++];
}

static void on_accept(hio_t* io) {
    hio_detach(io);

    hloop_t* worker_loop = get_next_loop();
    hevent_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.loop = worker_loop;
    ev.cb = new_conn_event;
    ev.userdata = io;
    hloop_post_event(worker_loop, &ev);
}

static HTHREAD_ROUTINE(worker_thread) {
    hloop_t* loop = (hloop_t*)userdata;
    hloop_run(loop);
    return 0;
}

static HTHREAD_ROUTINE(accept_thread) {
    hloop_t* loop = (hloop_t*)userdata;
    hio_t* listenio = hloop_create_tcp_server(loop, proxy_host, proxy_port, on_accept);
    if (listenio == NULL) {
        exit(1);
    }
    if (proxy_ssl) {
        hio_enable_ssl(listenio);
    }
    printf("tinyproxyd listening on %s:%d, listenfd=%d, thread_num=%d\n",
            proxy_host, proxy_port, hio_fd(listenio), thread_num);
    hloop_run(loop);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s proxy_port [thread_num]\n", argv[0]);
        return -10;
    }
    proxy_port = atoi(argv[1]);
    if (argc > 2) {
        thread_num = atoi(argv[2]);
    } else {
        thread_num = get_ncpu();
    }
    if (thread_num == 0) thread_num = 1;

    worker_loops = (hloop_t**)malloc(sizeof(hloop_t*) * thread_num);
    for (int i = 0; i < thread_num; ++i) {
        worker_loops[i] = hloop_new(HLOOP_FLAG_AUTO_FREE);
        hthread_create(worker_thread, worker_loops[i]);
    }

    accept_loop = hloop_new(HLOOP_FLAG_AUTO_FREE);
    accept_thread(accept_loop);
    return 0;
}
