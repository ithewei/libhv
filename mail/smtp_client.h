#ifndef HV_SMTP_CLIENT_H_
#define HV_SMTP_CLIENT_H_

#include "hloop.h"
#include "hssl.h"
#include "hmutex.h"
#include "mime.h"

#define DEFAULT_SMTP_PORT   25
#define DEFAULT_SMTPS_PORT  465

typedef struct smtp_client_s smtp_client_t;

// result callback: code>=200 && code<300 means success (SMTP 2xx)
// @param code:  SMTP status code, or negative libhv ERR_* on transport error
// @param msg:   last server response line (may be NULL)
typedef void (*smtp_client_cb)(smtp_client_t* cli, int code, const char* msg);

BEGIN_EXTERN_C

// hloop_new -> malloc(smtp_client_t)
HV_EXPORT smtp_client_t* smtp_client_new(hloop_t* loop DEFAULT(NULL));
HV_EXPORT void           smtp_client_run (smtp_client_t* cli);
HV_EXPORT void           smtp_client_stop(smtp_client_t* cli);
HV_EXPORT void           smtp_client_free(smtp_client_t* cli);

// auth
HV_EXPORT void smtp_client_set_auth(smtp_client_t* cli,
        const char* username, const char* password);

// callback (called once when the send finishes or fails)
HV_EXPORT void smtp_client_set_callback(smtp_client_t* cli, smtp_client_cb cb);

// userdata
HV_EXPORT void  smtp_client_set_userdata(smtp_client_t* cli, void* userdata);
HV_EXPORT void* smtp_client_get_userdata(smtp_client_t* cli);

// SSL/TLS (direct TLS; use port 465)
HV_EXPORT int smtp_client_set_ssl_ctx(smtp_client_t* cli, hssl_ctx_t ssl_ctx);
HV_EXPORT int smtp_client_new_ssl_ctx(smtp_client_t* cli, hssl_ctx_opt_t* opt);

// connect
HV_EXPORT void smtp_client_set_connect_timeout(smtp_client_t* cli, int ms);
HV_EXPORT void smtp_client_set_host(smtp_client_t* cli, const char* host, int port, int ssl);

// send: connect (if needed) and send the mail; result via callback.
// @retval 0 started ok, <0 error
HV_EXPORT int smtp_client_send(smtp_client_t* cli, mail_t* mail);

END_EXTERN_C

#ifdef __cplusplus

#include <functional>
#include <string>

namespace hv {

// @usage examples/mail/sendmail_test.cpp
class SmtpClient {
public:
    smtp_client_t* client;
    typedef std::function<void(SmtpClient*, int code, const std::string& msg)> ResultCallback;
    ResultCallback onResult;

    SmtpClient(hloop_t* loop = NULL) {
        client = smtp_client_new(loop);
    }
    ~SmtpClient() {
        if (client) {
            smtp_client_free(client);
            client = NULL;
        }
    }

    void setHost(const char* host, int port = DEFAULT_SMTPS_PORT, bool ssl = true) {
        smtp_client_set_host(client, host, port, ssl ? 1 : 0);
    }
    void setAuth(const char* username, const char* password) {
        smtp_client_set_auth(client, username, password);
    }
    void setConnectTimeout(int ms) {
        smtp_client_set_connect_timeout(client, ms);
    }
    int setSslCtx(hssl_ctx_t ssl_ctx) {
        return smtp_client_set_ssl_ctx(client, ssl_ctx);
    }
    int newSslCtx(hssl_ctx_opt_t* opt) {
        return smtp_client_new_ssl_ctx(client, opt);
    }

    int send(mail_t* mail) {
        smtp_client_set_callback(client, on_smtp);
        smtp_client_set_userdata(client, this);
        return smtp_client_send(client, mail);
    }

    void run()  { smtp_client_run(client); }
    void stop() { smtp_client_stop(client); }

private:
    static void on_smtp(smtp_client_t* cli, int code, const char* msg) {
        SmtpClient* self = (SmtpClient*)smtp_client_get_userdata(cli);
        if (self && self->onResult) {
            self->onResult(self, code, msg ? msg : "");
        }
    }
};

}

#endif // __cplusplus

#endif // HV_SMTP_CLIENT_H_
