/*
 * tinyhttpd tiny http server
 *
 * @build    make examples
 *
 * @server   bin/tinyhttpd 8000
 *
 * @client   bin/curl -v http://127.0.0.1:8000/
 *           bin/curl -v http://127.0.0.1:8000/ping
 *           bin/curl -v http://127.0.0.1:8000/echo -d "hello,world!"
 *
 * @webbench bin/wrk  http://127.0.0.1:8000/ping
 *
 */

#include "hv.h"
#include "hloop.h"

/*
 * workflow:
 * hloop_new -> hloop_create_tcp_server -> hloop_run ->
 * on_accept -> HV_ALLOC(http_conn_t) -> hio_readline ->
 * on_recv -> parse_http_request_line -> hio_readline ->
 * on_recv -> parse_http_head -> ...  -> hio_readbytes(content_length) ->
 * on_recv -> on_request -> http_reply-> hio_write -> hio_close ->
 * on_close -> HV_FREE(http_conn_t)
 *
 */

static const char* host = "0.0.0.0";
static int port = 8000;
static int thread_num = 1;
static hloop_t*  accept_loop = NULL;
static hloop_t** worker_loops = NULL;

#define HTTP_KEEPALIVE_TIMEOUT  60000 // ms
#define HTTP_MAX_URL_LENGTH     256
#define HTTP_MAX_HEAD_LENGTH    1024

#define HTML_TAG_BEGIN  "<html><body><center><h1>"
#define HTML_TAG_END    "</h1></center></body></html>"

// status_message
#define HTTP_OK         "OK"
#define NOT_FOUND       "Not Found"
#define NOT_IMPLEMENTED "Not Implemented"

// Content-Type
#define TEXT_PLAIN      "text/plain"
#define TEXT_HTML       "text/html"

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
//  char        head[HTTP_MAX_HEAD_LENGTH];
//  int         head_len;
    // body
    char*       body;
    int         body_len; // body_len = content_length
} http_msg_t;

typedef struct {
    hio_t*          io;
    http_state_e    state;
    http_msg_t      request;
    http_msg_t      response;
    // for http_serve_file
    FILE*           fp;
    hbuf_t          filebuf;
} http_conn_t;

static char s_date[32] = {0};
static void update_date(htimer_t* timer) {
    uint64_t now = hloop_now(hevent_loop(timer));
    gmtime_fmt(now, s_date);
}

static int http_response_dump(http_msg_t* msg, char* buf, int len) {
    int offset = 0;
    // status line
    offset += snprintf(buf + offset, len - offset, "HTTP/%d.%d %d %s\r\n", msg->major_version, msg->minor_version, msg->status_code, msg->status_message);
    // headers
    offset += snprintf(buf + offset, len - offset, "Server: libhv/%s\r\n", hv_version());
    offset += snprintf(buf + offset, len - offset, "Connection: %s\r\n", msg->keepalive ? "keep-alive" : "close");
    if (msg->content_length > 0) {
        offset += snprintf(buf + offset, len - offset, "Content-Length: %d\r\n", msg->content_length);
    }
    if (*msg->content_type) {
        offset += snprintf(buf + offset, len - offset, "Content-Type: %s\r\n", msg->content_type);
    }
    if (*s_date) {
        offset += snprintf(buf + offset, len - offset, "Date: %s\r\n", s_date);
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

static int http_reply(http_conn_t* conn,
            int status_code, const char* status_message,
            const char* content_type,
            const char* body, int body_len) {
    http_msg_t* req  = &conn->request;
    http_msg_t* resp = &conn->response;
    resp->major_version = req->major_version;
    resp->minor_version = req->minor_version;
    resp->status_code = status_code;
    if (status_message) strncpy(resp->status_message, status_message, sizeof(req->status_message) - 1);
    if (content_type)   strncpy(resp->content_type, content_type, sizeof(req->content_type) - 1);
    resp->keepalive = req->keepalive;
    if (body) {
        if (body_len <= 0) body_len = strlen(body);
        resp->content_length = body_len;
        resp->body = (char*)body;
    }
    char* buf = NULL;
    STACK_OR_HEAP_ALLOC(buf, HTTP_MAX_HEAD_LENGTH + resp->content_length, HTTP_MAX_HEAD_LENGTH + 1024);
    int msglen = http_response_dump(resp, buf, HTTP_MAX_HEAD_LENGTH + resp->content_length);
    int nwrite = hio_write(conn->io, buf, msglen);
    STACK_OR_HEAP_FREE(buf);
    return nwrite < 0 ? nwrite : msglen;
}

static void http_send_file(http_conn_t* conn) {
    if (!conn || !conn->fp) return;
    // alloc filebuf
    if (!conn->filebuf.base) {
        conn->filebuf.len = 4096;
        HV_ALLOC(conn->filebuf, conn->filebuf.len);
    }
    char* filebuf = conn->filebuf.base;
    size_t filebuflen = conn->filebuf.len;
    // read file
    int nread = fread(filebuf, 1, filebuflen, conn->fp);
    if (nread <= 0) {
        // eof or error
        hio_close(conn->io);
        return;
    }
    // send file
    hio_write(conn->io, filebuf, nread);
}

static void on_write(hio_t* io, const void* buf, int writebytes) {
    if (!io) return;
    if (!hio_write_is_complete(io)) return;
    http_conn_t* conn = (http_conn_t*)hevent_userdata(io);
    http_send_file(conn);
}

static int http_serve_file(http_conn_t* conn) {
    http_msg_t* req = &conn->request;
    http_msg_t* resp = &conn->response;
    // GET / HTTP/1.1\r\n
    const char* filepath = req->path + 1;
    // homepage
    if (*filepath == '\0') {
        filepath = "index.html";
    }
    // open file
    conn->fp = fopen(filepath, "rb");
    if (!conn->fp) {
        http_reply(conn, 404, NOT_FOUND, TEXT_HTML, HTML_TAG_BEGIN NOT_FOUND HTML_TAG_END, 0);
        return 404;
    }
    // send head
    size_t filesize = hv_filesize(filepath);
    resp->content_length = filesize;
    const char* suffix = hv_suffixname(filepath);
    const char* content_type = NULL;
    if (strcmp(suffix, "html") == 0) {
        content_type = TEXT_HTML;
    } else {
        // TODO: set content_type by suffix
    }
    hio_setcb_write(conn->io, on_write);
    int nwrite = http_reply(conn, 200, "OK", content_type, NULL, 0);
    if (nwrite < 0) return nwrite; // disconnected
    return 200;
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
    if (stricmp(key, "Content-Length") == 0) {
        req->content_length = atoi(val);
    } else if (stricmp(key, "Content-Type") == 0) {
        strncpy(req->content_type, val, sizeof(req->content_type) - 1);
    } else if (stricmp(key, "Connection") == 0) {
        if (stricmp(val, "close") == 0) {
            req->keepalive = 0;
        }
    } else {
        // TODO: save other head
    }
    return true;
}

static int on_request(http_conn_t* conn) {
    http_msg_t* req = &conn->request;
    // TODO: router
    if (strcmp(req->method, "GET") == 0) {
        // GET /ping HTTP/1.1\r\n
        if (strcmp(req->path, "/ping") == 0) {
            http_reply(conn, 200, "OK", TEXT_PLAIN, "pong", 4);
            return 200;
        } else {
            // TODO: Add handler for your path
        }
        return http_serve_file(conn);
    } else if (strcmp(req->method, "POST") == 0) {
        // POST /echo HTTP/1.1\r\n
        if (strcmp(req->path, "/echo") == 0) {
            http_reply(conn, 200, "OK", req->content_type, req->body, req->content_length);
            return 200;
        } else {
            // TODO: Add handler for your path
        }
    } else {
        // TODO: handle other method
    }
    http_reply(conn, 501, NOT_IMPLEMENTED, TEXT_HTML, HTML_TAG_BEGIN NOT_IMPLEMENTED HTML_TAG_END, 0);
    return 501;
}

static void on_close(hio_t* io) {
    // printf("on_close fd=%d error=%d\n", hio_fd(io), hio_error(io));
    http_conn_t* conn = (http_conn_t*)hevent_userdata(io);
    if (conn) {
        if (conn->fp) {
            // close file
            fclose(conn->fp);
            conn->fp = NULL;
        }
        // free filebuf
        HV_FREE(conn->filebuf.base);
        HV_FREE(conn);
        hevent_set_userdata(io, NULL);
    }
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
        if (req->content_length == 0) {
            conn->state = s_end;
            goto s_end;
        } else {
            // start read body
            conn->state = s_body;
            // WARN: too large content_length should read multiple times!
            hio_readbytes(io, req->content_length);
            break;
        }
    case s_body:
        // printf("s_body\n");
        req->body = str;
        req->body_len += readbytes;
        if (req->body_len == req->content_length) {
            conn->state = s_end;
        } else {
            // WARN: too large content_length should be handled by streaming!
            break;
        }
    case s_end:
s_end:
        // printf("s_end\n");
        // received complete request
        on_request(conn);
        if (hio_is_closed(io)) return;
        if (req->keepalive) {
            // Connection: keep-alive\r\n
            // reset and receive next request
            memset(&conn->request,  0, sizeof(http_msg_t));
            memset(&conn->response, 0, sizeof(http_msg_t));
            conn->state = s_first_line;
            hio_readline(io);
        } else {
            // Connection: close\r\n
            hio_close(io);
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
    hio_t* listenio = hloop_create_tcp_server(loop, host, port, on_accept);
    if (listenio == NULL) {
        exit(1);
    }
    printf("tinyhttpd listening on %s:%d, listenfd=%d, thread_num=%d\n",
            host, port, hio_fd(listenio), thread_num);
    // NOTE: add timer to update date every 1s
    htimer_add(loop, update_date, 1000, INFINITE);
    hloop_run(loop);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s port [thread_num]\n", argv[0]);
        return -10;
    }
    port = atoi(argv[1]);
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
