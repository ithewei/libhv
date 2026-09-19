#include "smtp_client.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "hbase.h"
#include "hlog.h"
#include "herr.h"
#include "hsocket.h"
#include "base64.h"

// SMTP send state machine
enum smtp_state {
    SMTP_ST_INIT = 0,
    SMTP_ST_GREETING,   // wait 220
    SMTP_ST_EHLO,       // wait 250
    SMTP_ST_AUTH,       // wait 334 (AUTH LOGIN)
    SMTP_ST_AUTH_USER,  // wait 334 (username sent)
    SMTP_ST_AUTH_PASS,  // wait 235 (password sent)
    SMTP_ST_MAIL_FROM,  // wait 250
    SMTP_ST_RCPT_TO,    // wait 250 (one per recipient)
    SMTP_ST_DATA,       // wait 354
    SMTP_ST_BODY,       // wait 250 (after EOB)
    SMTP_ST_QUIT,       // wait 221
    SMTP_ST_DONE,
};

struct smtp_client_s {
    char host[256];
    int  port;
    int  connect_timeout; // ms
    unsigned char ssl: 1;
    unsigned char alloced_ssl_ctx: 1;
    unsigned char is_loop_owner: 1;
    char username[128];
    char password[128];
    // state
    int  state;
    int  rcpt_index;      // current recipient being sent
    mail_t* mail;         // borrowed, not owned
    char* message;        // built MIME message (owned)
    char  last_msg[512];  // last server response line
    // callback
    smtp_client_cb cb;
    int  cb_called;
    void* userdata;
    // io
    hloop_t*  loop;
    hio_t*    io;
    htimer_t* timer;
    hssl_ctx_t ssl_ctx;
    hmutex_t  mutex_;
};

static void smtp_send_next(smtp_client_t* cli);

static void smtp_finish(smtp_client_t* cli, int code, const char* msg) {
    if (cli->cb && !cli->cb_called) {
        cli->cb_called = 1;
        cli->cb(cli, code, msg);
    }
}

static int smtp_write(smtp_client_t* cli, const char* buf, int len) {
    hmutex_lock(&cli->mutex_);
    int nwrite = cli->io ? hio_write(cli->io, buf, len) : -1;
    hmutex_unlock(&cli->mutex_);
    return nwrite;
}

static int smtp_writef(smtp_client_t* cli, const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (len <= 0) return -1;
    return smtp_write(cli, buf, len);
}

// parse leading 3-digit status code; returns -1 if not a complete final line.
// SMTP multiline: "250-..." continuation, "250 ..." final line.
static int smtp_parse_status(const char* data, int len, int* is_final) {
    if (len < 4) { *is_final = 0; return -1; }
    int code = (data[0]-'0')*100 + (data[1]-'0')*10 + (data[2]-'0');
    *is_final = (data[3] == ' ');
    return code;
}

static void on_recv(hio_t* io, void* buf, int len) {
    smtp_client_t* cli = (smtp_client_t*)hevent_userdata(io);
    if (cli == NULL) return;

    // A server response may contain multiple lines; only act on the final line
    // of the current reply. Scan for the last "NNN " (space after code) line.
    const char* data = (const char*)buf;
    int is_final = 0;
    int code = -1;
    const char* line = data;
    const char* p = data;
    const char* end = data + len;
    while (p < end) {
        const char* nl = memchr(p, '\n', end - p);
        int linelen = nl ? (int)(nl - p) : (int)(end - p);
        int fin = 0;
        int c = smtp_parse_status(p, linelen, &fin);
        if (c >= 0) {
            code = c;
            is_final = fin;
            line = p;
            // save last line text
            int cpy = linelen < (int)sizeof(cli->last_msg) - 1 ? linelen : (int)sizeof(cli->last_msg) - 1;
            memcpy(cli->last_msg, p, cpy);
            cli->last_msg[cpy] = '\0';
        }
        if (nl == NULL) break;
        p = nl + 1;
    }
    (void)line;
    if (code < 0 || !is_final) {
        // wait for more / not a final line
        return;
    }

    // dispatch by state; verify expected code, advance, send next command
    switch (cli->state) {
    case SMTP_ST_GREETING:
        if (code != 220) { smtp_finish(cli, code, cli->last_msg); hio_close(io); return; }
        cli->state = SMTP_ST_EHLO;
        smtp_writef(cli, "EHLO %s\r\n", cli->host);
        break;
    case SMTP_ST_EHLO:
        if (code != 250) { smtp_finish(cli, code, cli->last_msg); hio_close(io); return; }
        if (cli->username[0]) {
            cli->state = SMTP_ST_AUTH;
            smtp_writef(cli, "AUTH LOGIN\r\n");
        } else {
            cli->state = SMTP_ST_MAIL_FROM;
            smtp_writef(cli, "MAIL FROM:<%s>\r\n", cli->mail->from.addr);
        }
        break;
    case SMTP_ST_AUTH:
        if (code != 334) { smtp_finish(cli, code, cli->last_msg); hio_close(io); return; }
        {
            char b64[256];
            int n = hv_base64_encode((const unsigned char*)cli->username, strlen(cli->username), b64);
            cli->state = SMTP_ST_AUTH_USER;
            smtp_writef(cli, "%.*s\r\n", n, b64);
        }
        break;
    case SMTP_ST_AUTH_USER:
        if (code != 334) { smtp_finish(cli, code, cli->last_msg); hio_close(io); return; }
        {
            char b64[256];
            int n = hv_base64_encode((const unsigned char*)cli->password, strlen(cli->password), b64);
            cli->state = SMTP_ST_AUTH_PASS;
            smtp_writef(cli, "%.*s\r\n", n, b64);
        }
        break;
    case SMTP_ST_AUTH_PASS:
        if (code != 235) { smtp_finish(cli, code, cli->last_msg); hio_close(io); return; }
        cli->state = SMTP_ST_MAIL_FROM;
        smtp_writef(cli, "MAIL FROM:<%s>\r\n", cli->mail->from.addr);
        break;
    case SMTP_ST_MAIL_FROM:
        if (code != 250) { smtp_finish(cli, code, cli->last_msg); hio_close(io); return; }
        cli->state = SMTP_ST_RCPT_TO;
        cli->rcpt_index = 0;
        smtp_send_next(cli);   // send first RCPT TO
        break;
    case SMTP_ST_RCPT_TO:
        if (code != 250 && code != 251) { smtp_finish(cli, code, cli->last_msg); hio_close(io); return; }
        cli->rcpt_index++;
        smtp_send_next(cli);   // next RCPT or DATA
        break;
    case SMTP_ST_DATA:
        if (code != 354) { smtp_finish(cli, code, cli->last_msg); hio_close(io); return; }
        cli->state = SMTP_ST_BODY;
        // send message + end-of-body
        smtp_write(cli, cli->message, (int)strlen(cli->message));
        smtp_write(cli, "\r\n.\r\n", 5);
        break;
    case SMTP_ST_BODY:
        if (code != 250) { smtp_finish(cli, code, cli->last_msg); hio_close(io); return; }
        cli->state = SMTP_ST_QUIT;
        smtp_writef(cli, "QUIT\r\n");
        break;
    case SMTP_ST_QUIT:
        // 221 expected; report success regardless of QUIT ack
        cli->state = SMTP_ST_DONE;
        smtp_finish(cli, 250, cli->last_msg);
        hio_close(io);
        break;
    default:
        break;
    }
}

// send RCPT TO for cli->rcpt_index (to then cc), or advance to DATA.
static void smtp_send_next(smtp_client_t* cli) {
    mail_t* mail = cli->mail;
    int idx = cli->rcpt_index;
    if (idx < mail->to_count) {
        smtp_writef(cli, "RCPT TO:<%s>\r\n", mail->to[idx].addr);
        return;
    }
    idx -= mail->to_count;
    if (idx < mail->cc_count) {
        smtp_writef(cli, "RCPT TO:<%s>\r\n", mail->cc[idx].addr);
        return;
    }
    // all recipients done -> DATA
    cli->state = SMTP_ST_DATA;
    smtp_writef(cli, "DATA\r\n");
}

static void connect_timeout_cb(htimer_t* timer) {
    smtp_client_t* cli = (smtp_client_t*)hevent_userdata(timer);
    if (cli == NULL) return;
    cli->timer = NULL;
    hio_t* io = cli->io;
    if (io == NULL) return;
    hlogw("smtp connect timeout %s:%d", cli->host, cli->port);
    smtp_finish(cli, ERR_TASK_TIMEOUT, "connect timeout");
    hio_close(io);
}

static void on_connect(hio_t* io) {
    smtp_client_t* cli = (smtp_client_t*)hevent_userdata(io);
    if (cli == NULL) return;
    if (cli->timer) {
        htimer_del(cli->timer);
        cli->timer = NULL;
    }
    cli->state = SMTP_ST_GREETING;
    hio_setcb_read(io, on_recv);
    hio_read(io);
}

static void on_close(hio_t* io) {
    smtp_client_t* cli = (smtp_client_t*)hevent_userdata(io);
    if (cli == NULL) return;
    // if closed before finishing, report failure
    if (!cli->cb_called) {
        smtp_finish(cli, ERR_CONNECT, cli->last_msg[0] ? cli->last_msg : "connection closed");
    }
    cli->io = NULL;
}

smtp_client_t* smtp_client_new(hloop_t* loop) {
    int is_loop_owner = (loop == NULL);
    if (loop == NULL) {
        loop = hloop_new(HLOOP_FLAG_AUTO_FREE);
        if (loop == NULL) return NULL;
    }
    smtp_client_t* cli = NULL;
    HV_ALLOC_SIZEOF(cli);
    if (cli == NULL) return NULL;
    cli->loop = loop;
    cli->is_loop_owner = is_loop_owner;
    cli->port = DEFAULT_SMTPS_PORT;
    cli->ssl = 1;
    hmutex_init(&cli->mutex_);
    return cli;
}

void smtp_client_free(smtp_client_t* cli) {
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
    HV_FREE(cli->message);
    HV_FREE(cli);
}

void smtp_client_run(smtp_client_t* cli) {
    if (!cli || !cli->loop) return;
    if (!cli->is_loop_owner) return;
    hloop_run(cli->loop);
    cli->loop = NULL;
    cli->io = NULL;
    cli->timer = NULL;
}

void smtp_client_stop(smtp_client_t* cli) {
    if (!cli || !cli->loop) return;
    if (!cli->is_loop_owner) return;
    hloop_stop(cli->loop);
}

void smtp_client_set_auth(smtp_client_t* cli, const char* username, const char* password) {
    if (!cli) return;
    if (username) hv_strncpy(cli->username, username, sizeof(cli->username));
    if (password) hv_strncpy(cli->password, password, sizeof(cli->password));
}

void smtp_client_set_callback(smtp_client_t* cli, smtp_client_cb cb) {
    if (cli) cli->cb = cb;
}

void smtp_client_set_userdata(smtp_client_t* cli, void* userdata) {
    if (cli) cli->userdata = userdata;
}

void* smtp_client_get_userdata(smtp_client_t* cli) {
    return cli ? cli->userdata : NULL;
}

int smtp_client_set_ssl_ctx(smtp_client_t* cli, hssl_ctx_t ssl_ctx) {
    cli->ssl_ctx = ssl_ctx;
    return 0;
}

int smtp_client_new_ssl_ctx(smtp_client_t* cli, hssl_ctx_opt_t* opt) {
    opt->endpoint = HSSL_CLIENT;
    hssl_ctx_t ssl_ctx = hssl_ctx_new(opt);
    if (ssl_ctx == NULL) return ERR_NEW_SSL_CTX;
    cli->alloced_ssl_ctx = 1;
    return smtp_client_set_ssl_ctx(cli, ssl_ctx);
}

void smtp_client_set_connect_timeout(smtp_client_t* cli, int ms) {
    if (cli) cli->connect_timeout = ms;
}

void smtp_client_set_host(smtp_client_t* cli, const char* host, int port, int ssl) {
    if (!cli) return;
    hv_strncpy(cli->host, host, sizeof(cli->host));
    cli->port = port;
    cli->ssl = ssl ? 1 : 0;
}

int smtp_client_send(smtp_client_t* cli, mail_t* mail) {
    if (!cli || !mail) return -1;
    if (!cli->host[0]) return ERR_INVALID_PARAM;
    if (!mail->from.addr || (mail->to_count == 0 && mail->cc_count == 0)) return ERR_INVALID_PARAM;

    cli->mail = mail;
    cli->cb_called = 0;
    cli->state = SMTP_ST_INIT;
    cli->rcpt_index = 0;
    cli->last_msg[0] = '\0';

    HV_FREE(cli->message);
    cli->message = mime_build(mail);
    if (cli->message == NULL) return ERR_NULL_POINTER;

    hio_t* io = hio_create_socket(cli->loop, cli->host, cli->port, HIO_TYPE_TCP, HIO_CLIENT_SIDE);
    if (io == NULL) return ERR_SOCKET;
    if (cli->ssl) {
        if (cli->ssl_ctx) hio_set_ssl_ctx(io, cli->ssl_ctx);
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
