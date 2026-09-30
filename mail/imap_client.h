#ifndef HV_IMAP_CLIENT_H_
#define HV_IMAP_CLIENT_H_

#include "hloop.h"
#include "hssl.h"
#include "hmutex.h"
#include "mime.h"

#define DEFAULT_IMAP_PORT   143
#define DEFAULT_IMAPS_PORT  993

typedef struct imap_client_s imap_client_t;

// per-mail callback: called once for each fetched message.
// mail is owned by the client and valid only during the callback.
typedef void (*imap_mail_cb)(imap_client_t* cli, mail_t* mail);
// done/result callback: called once when fetch finishes or fails.
// @param code: 0 success; <0 libhv ERR_*; >0 not used
typedef void (*imap_done_cb)(imap_client_t* cli, int code, const char* msg);

BEGIN_EXTERN_C

HV_EXPORT imap_client_t* imap_client_new(hloop_t* loop DEFAULT(NULL));
HV_EXPORT void           imap_client_run (imap_client_t* cli);
HV_EXPORT void           imap_client_stop(imap_client_t* cli);
HV_EXPORT void           imap_client_free(imap_client_t* cli);

HV_EXPORT void imap_client_set_auth(imap_client_t* cli,
        const char* username, const char* password);
HV_EXPORT void imap_client_set_mail_callback(imap_client_t* cli, imap_mail_cb cb);
HV_EXPORT void imap_client_set_done_callback(imap_client_t* cli, imap_done_cb cb);
HV_EXPORT void  imap_client_set_userdata(imap_client_t* cli, void* userdata);
HV_EXPORT void* imap_client_get_userdata(imap_client_t* cli);

HV_EXPORT int imap_client_set_ssl_ctx(imap_client_t* cli, hssl_ctx_t ssl_ctx);
HV_EXPORT int imap_client_new_ssl_ctx(imap_client_t* cli, hssl_ctx_opt_t* opt);

HV_EXPORT void imap_client_set_connect_timeout(imap_client_t* cli, int ms);
HV_EXPORT void imap_client_set_host(imap_client_t* cli, const char* host, int port, int ssl);

// fetch: LOGIN -> SELECT mailbox -> SEARCH criteria -> FETCH each -> LOGOUT.
// @param mailbox:  e.g. "INBOX"
// @param criteria: IMAP SEARCH criteria, e.g. "ALL", "UNSEEN"
// @retval 0 started ok, <0 error
HV_EXPORT int imap_client_fetch(imap_client_t* cli, const char* mailbox, const char* criteria);

END_EXTERN_C

#ifdef __cplusplus

#include <functional>
#include <string>

namespace hv {

// @usage examples/mail/recvmail_test.cpp
class ImapClient {
public:
    imap_client_t* client;
    typedef std::function<void(ImapClient*, mail_t*)> MailCallback;
    typedef std::function<void(ImapClient*, int code, const std::string& msg)> DoneCallback;
    MailCallback onMail;
    DoneCallback onDone;

    ImapClient(hloop_t* loop = NULL) {
        client = imap_client_new(loop);
    }
    ~ImapClient() {
        if (client) {
            imap_client_free(client);
            client = NULL;
        }
    }

    void setHost(const char* host, int port = DEFAULT_IMAPS_PORT, bool ssl = true) {
        imap_client_set_host(client, host, port, ssl ? 1 : 0);
    }
    void setAuth(const char* username, const char* password) {
        imap_client_set_auth(client, username, password);
    }
    void setConnectTimeout(int ms) {
        imap_client_set_connect_timeout(client, ms);
    }
    int setSslCtx(hssl_ctx_t ssl_ctx) {
        return imap_client_set_ssl_ctx(client, ssl_ctx);
    }
    int newSslCtx(hssl_ctx_opt_t* opt) {
        return imap_client_new_ssl_ctx(client, opt);
    }

    int fetch(const char* mailbox = "INBOX", const char* criteria = "ALL") {
        imap_client_set_mail_callback(client, on_mail);
        imap_client_set_done_callback(client, on_done);
        imap_client_set_userdata(client, this);
        return imap_client_fetch(client, mailbox, criteria);
    }

    void run()  { imap_client_run(client); }
    void stop() { imap_client_stop(client); }

private:
    static void on_mail(imap_client_t* cli, mail_t* mail) {
        ImapClient* self = (ImapClient*)imap_client_get_userdata(cli);
        if (self && self->onMail) self->onMail(self, mail);
    }
    static void on_done(imap_client_t* cli, int code, const char* msg) {
        ImapClient* self = (ImapClient*)imap_client_get_userdata(cli);
        if (self && self->onDone) self->onDone(self, code, msg ? msg : "");
    }
};

}

#endif // __cplusplus

#endif // HV_IMAP_CLIENT_H_
