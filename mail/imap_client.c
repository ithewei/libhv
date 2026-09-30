#include "imap_client.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "hbase.h"
#include "hlog.h"
#include "herr.h"
#include "hsocket.h"

// IMAP fetch state machine
enum imap_state {
    IMAP_ST_INIT = 0,
    IMAP_ST_GREETING,   // wait untagged * OK
    IMAP_ST_LOGIN,      // wait tagged OK
    IMAP_ST_SELECT,     // wait tagged OK
    IMAP_ST_SEARCH,     // wait * SEARCH ... + tagged OK
    IMAP_ST_FETCH,      // wait literal message + tagged OK (per id)
    IMAP_ST_LOGOUT,     // wait tagged OK
    IMAP_ST_DONE,
};

#define IMAP_MAX_IDS  4096

struct imap_client_s {
    char host[256];
    int  port;
    int  connect_timeout;
    unsigned char ssl: 1;
    unsigned char alloced_ssl_ctx: 1;
    unsigned char is_loop_owner: 1;
    char username[128];
    char password[128];
    char mailbox[128];
    char criteria[128];
    // state
    int  state;
    int  tag;             // current command tag number
    // search result ids
    int* ids;
    int  id_count;
    int  id_index;        // current fetch index
    // recv accumulation buffer (plain C growable buffer)
    char*  recvbuf;
    size_t recvcap;
    size_t recvlen;
    // callbacks
    imap_mail_cb  mail_cb;
    imap_done_cb  done_cb;
    int  done_called;
    void* userdata;
    // io
    hloop_t*  loop;
    hio_t*    io;
    htimer_t* timer;
    hssl_ctx_t ssl_ctx;
    hmutex_t  mutex_;
};

static void imap_fetch_next(imap_client_t* cli);
static void imap_process(imap_client_t* cli);

// IMAP quoted-string: wrap in double quotes and backslash-escape " and \.
// Caller must ensure the input has no control chars (see imap_has_ctrl).
static void imap_quote(const char* in, char* out, int outlen) {
    int o = 0;
    if (o < outlen - 1) out[o++] = '"';
    for (const char* p = in; *p && o < outlen - 2; ++p) {
        if (*p == '"' || *p == '\\') {
            if (o < outlen - 3) out[o++] = '\\';
        }
        out[o++] = *p;
    }
    if (o < outlen - 1) out[o++] = '"';
    out[o] = '\0';
}

// reject CR/LF and other control characters (command injection guard)
static int imap_has_ctrl(const char* s) {
    for (const char* p = s; *p; ++p) {
        if ((unsigned char)*p < 0x20) return 1;
    }
    return 0;
}

static void imap_done(imap_client_t* cli, int code, const char* msg) {
    if (cli->done_cb && !cli->done_called) {
        cli->done_called = 1;
        cli->done_cb(cli, code, msg);
    }
}

static int imap_write(imap_client_t* cli, const char* buf, int len) {
    hmutex_lock(&cli->mutex_);
    int nwrite = cli->io ? hio_write(cli->io, buf, len) : -1;
    hmutex_unlock(&cli->mutex_);
    return nwrite;
}

// send "aNNN <cmd>\r\n"
static int imap_send_cmd(imap_client_t* cli, const char* cmd) {
    char buf[512];
    int len = snprintf(buf, sizeof(buf), "a%03d %s\r\n", ++cli->tag, cmd);
    return imap_write(cli, buf, len);
}

// build current tag prefix "aNNN "
static int imap_tag_prefix(imap_client_t* cli, char* out, int outlen) {
    return snprintf(out, outlen, "a%03d ", cli->tag);
}

// check whether buffer [data,data+len) contains the tagged final response for
// the current tag. Sets *ok to 1 if "aNNN OK", 0 if NO/BAD. Returns pointer to
// the byte just after that line's CRLF, or NULL if not present yet.
// NOTE: only a complete LF-terminated line is treated as final, so a tagged
// response split across reads is not consumed prematurely (which would discard
// the remainder and hang waiting for the next tag).
static char* find_tagged_response(imap_client_t* cli, char* data, size_t len, int* ok) {
    char prefix[16];
    int plen = imap_tag_prefix(cli, prefix, sizeof(prefix));
    char* p = data;
    char* end = data + len;
    while (p < end) {
        char* nl = (char*)memchr(p, '\n', end - p);
        if (nl == NULL) break;   // incomplete line; wait for more data
        int linelen = (int)(nl - p);
        if (linelen >= plen && strncmp(p, prefix, plen) == 0) {
            const char* status = p + plen;
            if (strnicmp(status, "OK", 2) == 0) *ok = 1;
            else *ok = 0;  // NO / BAD
            return nl + 1;
        }
        p = nl + 1;
    }
    return NULL;
}

// parse "* SEARCH 1 2 3\r\n" ids into cli->ids
static void parse_search(imap_client_t* cli, char* data, size_t len) {
    char* p = data;
    char* end = data + len;
    while (p < end) {
        char* nl = (char*)memchr(p, '\n', end - p);
        int linelen = nl ? (int)(nl - p) : (int)(end - p);
        if (linelen >= 8 && strncmp(p, "* SEARCH", 8) == 0) {
            const char* q = p + 8;
            const char* le = p + linelen;
            while (q < le) {
                while (q < le && (*q < '0' || *q > '9')) q++;
                if (q >= le) break;
                int id = 0;
                while (q < le && *q >= '0' && *q <= '9') { id = id * 10 + (*q - '0'); q++; }
                if (cli->id_count < IMAP_MAX_IDS) {
                    int* np = (int*)realloc(cli->ids, sizeof(int) * (cli->id_count + 1));
                    if (np) { cli->ids = np; cli->ids[cli->id_count++] = id; }
                }
            }
        }
        if (nl == NULL) break;
        p = nl + 1;
    }
}

// For FETCH: response looks like
//   * N FETCH (BODY[] {SIZE}\r\n<SIZE bytes>)\r\n
//   aNNN OK ...
// We need the literal {SIZE} then SIZE raw bytes. Returns 1 if a full FETCH
// message + tagged OK is available and was processed; 0 if need more data.
static int try_process_fetch(imap_client_t* cli, char* data, size_t len) {
    // find "{SIZE}\r\n"
    char* brace = (char*)memchr(data, '{', len);
    if (brace == NULL) {
        // maybe no literal (e.g. empty) — check for tagged response to finish
        int ok = 0;
        char* after = find_tagged_response(cli, data, len, &ok);
        if (after) {
            // consume everything up to tagged line; no mail parsed
            size_t consumed = after - data;
            memmove(data, after, len - consumed);
            cli->recvlen = len - consumed;
            return 1;
        }
        return 0;
    }
    char* end = data + len;
    // parse size
    char* q = brace + 1;
    long size = 0;
    while (q < end && *q >= '0' && *q <= '9') { size = size * 10 + (*q - '0'); q++; }
    if (q >= end || *q != '}') return 0;      // incomplete literal header
    // skip "}\r\n"
    q++;
    if (q < end && *q == '\r') q++;
    if (q >= end || *q != '\n') return 0;     // incomplete
    q++;
    // need `size` bytes of message
    if ((long)(end - q) < size) return 0;     // wait for more
    char* msg = q;
    // parse the message via MIME
    mail_t mail;
    memset(&mail, 0, sizeof(mail));
    if (mime_parse(msg, (size_t)size, &mail) == 0) {
        if (cli->mail_cb) cli->mail_cb(cli, &mail);
    }
    mail_clear(&mail);

    // after literal: expect ")\r\n" then tagged "aNNN OK"
    char* rest = msg + size;
    int ok = 0;
    char* after = find_tagged_response(cli, rest, end - rest, &ok);
    if (after == NULL) {
        // tagged response not fully received yet; keep the tail from rest
        size_t consumed = rest - data;
        memmove(data, rest, len - consumed);
        cli->recvlen = len - consumed;
        return 0;  // wait for tagged OK
    }
    // fully consumed this fetch
    size_t consumed = after - data;
    memmove(data, after, len - consumed);
    cli->recvlen = len - consumed;
    return 1;
}

static void imap_process(imap_client_t* cli) {
    char* data = cli->recvbuf;
    size_t len = cli->recvlen;
    int ok = 0;

    switch (cli->state) {
    case IMAP_ST_GREETING: {
        // wait for untagged "* OK"
        char* nl = (char*)memchr(data, '\n', len);
        if (nl == NULL) return;
        if (len >= 4 && strncmp(data, "* OK", 4) != 0) {
            imap_done(cli, ERR_RESPONSE, "no IMAP greeting");
            hio_close(cli->io);
            return;
        }
        // consume greeting line
        size_t consumed = nl - data + 1;
        memmove(data, nl + 1, len - consumed);
        cli->recvlen = len - consumed;
        // LOGIN with quoted credentials (escape " and \; control chars are
        // rejected earlier in imap_client_fetch to prevent command injection)
        cli->state = IMAP_ST_LOGIN;
        char user_q[256], pass_q[256];
        imap_quote(cli->username, user_q, sizeof(user_q));
        imap_quote(cli->password, pass_q, sizeof(pass_q));
        char cmd[600];
        snprintf(cmd, sizeof(cmd), "LOGIN %s %s", user_q, pass_q);
        imap_send_cmd(cli, cmd);
        break;
    }
    case IMAP_ST_LOGIN: {
        char* after = find_tagged_response(cli, data, len, &ok);
        if (after == NULL) return;
        if (!ok) { imap_done(cli, ERR_RESPONSE, "IMAP LOGIN failed"); hio_close(cli->io); return; }
        cli->recvlen = 0;   // discard
        cli->state = IMAP_ST_SELECT;
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "SELECT %s", cli->mailbox);
        imap_send_cmd(cli, cmd);
        break;
    }
    case IMAP_ST_SELECT: {
        char* after = find_tagged_response(cli, data, len, &ok);
        if (after == NULL) return;
        if (!ok) { imap_done(cli, ERR_RESPONSE, "IMAP SELECT failed"); hio_close(cli->io); return; }
        cli->recvlen = 0;
        cli->state = IMAP_ST_SEARCH;
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "SEARCH %s", cli->criteria);
        imap_send_cmd(cli, cmd);
        break;
    }
    case IMAP_ST_SEARCH: {
        char* after = find_tagged_response(cli, data, len, &ok);
        if (after == NULL) return;
        if (!ok) { imap_done(cli, ERR_RESPONSE, "IMAP SEARCH failed"); hio_close(cli->io); return; }
        parse_search(cli, data, after - data);
        cli->recvlen = 0;
        cli->id_index = 0;
        cli->state = IMAP_ST_FETCH;
        imap_fetch_next(cli);
        break;
    }
    case IMAP_ST_FETCH: {
        if (try_process_fetch(cli, data, len)) {
            // this id done; fetch next or logout
            cli->id_index++;
            imap_fetch_next(cli);
        }
        break;
    }
    case IMAP_ST_LOGOUT: {
        char* after = find_tagged_response(cli, data, len, &ok);
        if (after == NULL) return;
        cli->recvlen = 0;
        cli->state = IMAP_ST_DONE;
        imap_done(cli, 0, "OK");
        hio_close(cli->io);
        break;
    }
    default:
        break;
    }
}

static void imap_fetch_next(imap_client_t* cli) {
    if (cli->id_index >= cli->id_count) {
        // all fetched -> LOGOUT
        cli->state = IMAP_ST_LOGOUT;
        cli->recvlen = 0;
        imap_send_cmd(cli, "LOGOUT");
        return;
    }
    int id = cli->ids[cli->id_index];
    char cmd[64];
    // BODY.PEEK[] fetches the full message without setting the \Seen flag,
    // so reading mail does not mark it as read on the server.
    snprintf(cmd, sizeof(cmd), "FETCH %d BODY.PEEK[]", id);
    imap_send_cmd(cli, cmd);
}

static void on_recv(hio_t* io, void* buf, int len) {
    imap_client_t* cli = (imap_client_t*)hevent_userdata(io);
    if (cli == NULL) return;

    // accumulate into recvbuf (IMAP responses/literals may span reads)
    size_t need = cli->recvlen + len + 1;
    if (need > cli->recvcap) {
        size_t newcap = cli->recvcap ? cli->recvcap : 8192;
        while (newcap < need) newcap *= 2;
        char* np = (char*)realloc(cli->recvbuf, newcap);
        if (np == NULL) return;
        cli->recvbuf = np;
        cli->recvcap = newcap;
    }
    memcpy(cli->recvbuf + cli->recvlen, buf, len);
    cli->recvlen += len;
    cli->recvbuf[cli->recvlen] = '\0';

    // drive the state machine; loop while progress can be made in FETCH
    size_t prev;
    do {
        prev = cli->recvlen;
        imap_process(cli);
    } while (cli->state == IMAP_ST_FETCH && cli->recvlen != prev && cli->recvlen > 0);
}

static void connect_timeout_cb(htimer_t* timer) {
    imap_client_t* cli = (imap_client_t*)hevent_userdata(timer);
    if (cli == NULL) return;
    cli->timer = NULL;
    if (cli->io == NULL) return;
    hlogw("imap connect timeout %s:%d", cli->host, cli->port);
    imap_done(cli, ERR_TASK_TIMEOUT, "connect timeout");
    hio_close(cli->io);
}

static void on_connect(hio_t* io) {
    imap_client_t* cli = (imap_client_t*)hevent_userdata(io);
    if (cli == NULL) return;
    if (cli->timer) {
        htimer_del(cli->timer);
        cli->timer = NULL;
    }
    cli->state = IMAP_ST_GREETING;
    hio_setcb_read(io, on_recv);
    hio_read(io);
}

static void on_close(hio_t* io) {
    imap_client_t* cli = (imap_client_t*)hevent_userdata(io);
    if (cli == NULL) return;
    if (!cli->done_called) {
        imap_done(cli, ERR_CONNECT, "connection closed");
    }
    cli->io = NULL;
}

imap_client_t* imap_client_new(hloop_t* loop) {
    int is_loop_owner = (loop == NULL);
    if (loop == NULL) {
        loop = hloop_new(HLOOP_FLAG_AUTO_FREE);
        if (loop == NULL) return NULL;
    }
    imap_client_t* cli = NULL;
    HV_ALLOC_SIZEOF(cli);
    if (cli == NULL) return NULL;
    cli->loop = loop;
    cli->is_loop_owner = is_loop_owner;
    cli->port = DEFAULT_IMAPS_PORT;
    cli->ssl = 1;
    hv_strncpy(cli->mailbox, "INBOX", sizeof(cli->mailbox));
    hv_strncpy(cli->criteria, "ALL", sizeof(cli->criteria));
    hmutex_init(&cli->mutex_);
    return cli;
}

void imap_client_free(imap_client_t* cli) {
    if (!cli) return;
    if (cli->timer) {
        hevent_set_userdata(cli->timer, NULL);
        htimer_del(cli->timer);
        cli->timer = NULL;
    }
    if (cli->io) {
        hevent_set_userdata(cli->io, NULL);
        hio_setcb_close(cli->io, NULL);
        hio_close(cli->io);
        cli->io = NULL;
    }
    hmutex_destroy(&cli->mutex_);
    if (cli->ssl_ctx && cli->alloced_ssl_ctx) {
        hssl_ctx_free(cli->ssl_ctx);
        cli->ssl_ctx = NULL;
    }
    free(cli->ids);
    free(cli->recvbuf);
    HV_FREE(cli);
}

void imap_client_run(imap_client_t* cli) {
    if (!cli || !cli->loop) return;
    if (!cli->is_loop_owner) return;
    hloop_run(cli->loop);
    cli->loop = NULL;
    cli->io = NULL;
    cli->timer = NULL;
}

void imap_client_stop(imap_client_t* cli) {
    if (!cli || !cli->loop) return;
    if (!cli->is_loop_owner) return;
    hloop_stop(cli->loop);
}

void imap_client_set_auth(imap_client_t* cli, const char* username, const char* password) {
    if (!cli) return;
    if (username) hv_strncpy(cli->username, username, sizeof(cli->username));
    if (password) hv_strncpy(cli->password, password, sizeof(cli->password));
}

void imap_client_set_mail_callback(imap_client_t* cli, imap_mail_cb cb) {
    if (cli) cli->mail_cb = cb;
}
void imap_client_set_done_callback(imap_client_t* cli, imap_done_cb cb) {
    if (cli) cli->done_cb = cb;
}
void imap_client_set_userdata(imap_client_t* cli, void* userdata) {
    if (cli) cli->userdata = userdata;
}
void* imap_client_get_userdata(imap_client_t* cli) {
    return cli ? cli->userdata : NULL;
}

int imap_client_set_ssl_ctx(imap_client_t* cli, hssl_ctx_t ssl_ctx) {
    cli->ssl_ctx = ssl_ctx;
    return 0;
}
int imap_client_new_ssl_ctx(imap_client_t* cli, hssl_ctx_opt_t* opt) {
    opt->endpoint = HSSL_CLIENT;
    hssl_ctx_t ssl_ctx = hssl_ctx_new(opt);
    if (ssl_ctx == NULL) return ERR_NEW_SSL_CTX;
    cli->alloced_ssl_ctx = 1;
    return imap_client_set_ssl_ctx(cli, ssl_ctx);
}

void imap_client_set_connect_timeout(imap_client_t* cli, int ms) {
    if (cli) cli->connect_timeout = ms;
}

void imap_client_set_host(imap_client_t* cli, const char* host, int port, int ssl) {
    if (!cli) return;
    hv_strncpy(cli->host, host, sizeof(cli->host));
    cli->port = port;
    cli->ssl = ssl ? 1 : 0;
}

int imap_client_fetch(imap_client_t* cli, const char* mailbox, const char* criteria) {
    if (!cli) return -1;
    if (!cli->host[0] || !cli->username[0]) return ERR_INVALID_PARAM;
    // reject control characters in credentials (IMAP command injection guard)
    if (imap_has_ctrl(cli->username) || imap_has_ctrl(cli->password)) return ERR_INVALID_PARAM;
    if (mailbox)  hv_strncpy(cli->mailbox, mailbox, sizeof(cli->mailbox));
    if (criteria) hv_strncpy(cli->criteria, criteria, sizeof(cli->criteria));

    cli->state = IMAP_ST_INIT;
    cli->tag = 0;
    cli->done_called = 0;
    cli->id_count = 0;
    cli->id_index = 0;
    free(cli->ids);
    cli->ids = NULL;
    cli->recvlen = 0;

    hio_t* io = hio_create_socket(cli->loop, cli->host, cli->port, HIO_TYPE_TCP, HIO_CLIENT_SIDE);
    if (io == NULL) return ERR_SOCKET;
    if (cli->ssl) {
        if (cli->ssl_ctx) hio_set_ssl_ctx(io, cli->ssl_ctx);
        // set SNI hostname (see smtp_client for rationale)
        hio_set_hostname(io, cli->host);
        hio_enable_ssl(io);
    }
    cli->io = io;
    hevent_set_userdata(io, cli);
    hio_setcb_connect(io, on_connect);
    hio_setcb_close(io, on_close);
    if (cli->connect_timeout > 0) {
        cli->timer = htimer_add(cli->loop, connect_timeout_cb, cli->connect_timeout, 1);
        hevent_set_userdata(cli->timer, cli);
    }
    return hio_connect(io) < 0 ? ERR_SOCKET : 0;
}
