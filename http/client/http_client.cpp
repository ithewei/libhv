#include "http_client.h"

#include <mutex>

#ifdef WITH_CURL
#include "curl/curl.h"
#endif

#include "herr.h"
#include "hlog.h"
#include "hstring.h"
#include "hsocket.h"
#include "hssl.h"
#include "HttpParser.h"

// for async
#include "AsyncHttpClient.h"

using namespace hv;

struct http_client_s {
    std::string  host;
    int          port;
    int          https;
    int          timeout; // s
    http_headers headers;
    // http_proxy
    std::string  http_proxy_host;
    int          http_proxy_port;
    // https_proxy
    std::string  https_proxy_host;
    int          https_proxy_port;
    // no_proxy
    StringList   no_proxy_hosts;
//private:
#ifdef WITH_CURL
    CURL* curl;
#endif
    // for sync
    int             fd;
    hssl_t          ssl;
    hssl_ctx_t      ssl_ctx;
    bool            alloced_ssl_ctx;
    HttpParserPtr   parser;
    // for async
    std::mutex                              mutex_;
    std::shared_ptr<hv::AsyncHttpClient>    async_client_;

    http_client_s() {
        host = LOCALHOST;
        port = DEFAULT_HTTP_PORT;
        https = 0;
        timeout = DEFAULT_HTTP_TIMEOUT;
#ifdef WITH_CURL
        curl = NULL;
#endif
        fd = -1;
        ssl = NULL;
        ssl_ctx = NULL;
        alloced_ssl_ctx = false;
    }

    ~http_client_s() {
        Close();
        if (ssl_ctx && alloced_ssl_ctx) {
            hssl_ctx_free(ssl_ctx);
            ssl_ctx = NULL;
        }
    }

    void Close() {
#ifdef WITH_CURL
        if (curl) {
            curl_easy_cleanup(curl);
            curl = NULL;
        }
#endif
        if (ssl) {
            hssl_free(ssl);
            ssl = NULL;
        }
        SAFE_CLOSESOCKET(fd);
    }
};

static int __http_client_send(http_client_t* cli, HttpRequest* req, HttpResponse* resp);

http_client_t* http_client_new(const char* host, int port, int https) {
    http_client_t* cli = new http_client_t;
    if (host) cli->host = host;
    cli->port = port;
    cli->https = https;
    cli->headers["Connection"] = "keep-alive";
    return cli;
}

int http_client_del(http_client_t* cli) {
    if (cli == NULL) return 0;
    delete cli;
    return 0;
}

int http_client_close(http_client_t* cli) {
    if (cli == NULL) return 0;
    cli->Close();
    return 0;
}

int http_client_set_timeout(http_client_t* cli, int timeout) {
    cli->timeout = timeout;
    return 0;
}

int http_client_set_ssl_ctx(http_client_t* cli, hssl_ctx_t ssl_ctx) {
    cli->ssl_ctx = ssl_ctx;
    return 0;
}

int http_client_new_ssl_ctx(http_client_t* cli, hssl_ctx_opt_t* opt) {
    opt->endpoint = HSSL_CLIENT;
    hssl_ctx_t ssl_ctx = hssl_ctx_new(opt);
    if (ssl_ctx == NULL) return ERR_NEW_SSL_CTX;
    cli->alloced_ssl_ctx = true;
    return http_client_set_ssl_ctx(cli, ssl_ctx);
}

int http_client_clear_headers(http_client_t* cli) {
    cli->headers.clear();
    return 0;
}

int http_client_set_header(http_client_t* cli, const char* key, const char* value) {
    cli->headers[key] = value;
    return 0;
}

int http_client_del_header(http_client_t* cli, const char* key) {
    auto iter = cli->headers.find(key);
    if (iter != cli->headers.end()) {
        cli->headers.erase(iter);
    }
    return 0;
}

const char* http_client_get_header(http_client_t* cli, const char* key) {
    auto iter = cli->headers.find(key);
    if (iter != cli->headers.end()) {
        return iter->second.c_str();
    }
    return NULL;
}

int http_client_set_http_proxy(http_client_t* cli, const char* host, int port) {
    cli->http_proxy_host = host;
    cli->http_proxy_port = port;
    return 0;
}

int http_client_set_https_proxy(http_client_t* cli, const char* host, int port) {
    cli->https_proxy_host = host;
    cli->https_proxy_port = port;
    return 0;
}

int http_client_add_no_proxy(http_client_t* cli, const char* host) {
    cli->no_proxy_hosts.push_back(host);
    return 0;
}

static int http_client_redirect(HttpRequest* req, HttpResponse* resp) {
    std::string location = resp->headers["Location"];
    if (!location.empty()) {
        hlogi("redirect %s => %s", req->url.c_str(), location.c_str());
        req->url = location;
        req->ParseUrl();
        req->headers["Host"] = req->host;
        resp->Reset();
        return http_client_send(req, resp);
    }
    return 0;
}

static int http_client_make_request(http_client_t* cli, HttpRequest* req) {
    if (req->url.empty() || *req->url.c_str() == '/') {
        req->scheme = cli->https ? "https" : "http";
        req->host = cli->host;
        req->port = cli->port;
    }
    req->ParseUrl();

    bool https = req->IsHttps();
    bool use_proxy = https ? (!cli->https_proxy_host.empty()) : (!cli->http_proxy_host.empty());
    if (use_proxy) {
        if (req->host == "127.0.0.1" || req->host == "localhost") {
            use_proxy = false;
        }
    }
    if (use_proxy) {
        for (const auto& host : cli->no_proxy_hosts) {
            if (req->host == host) {
                use_proxy = false;
                break;
            }
        }
    }
    if (use_proxy) {
        req->SetProxy(https ? cli->https_proxy_host.c_str() : cli->http_proxy_host.c_str(),
                      https ? cli->https_proxy_port         : cli->http_proxy_port);
    }

    if (req->timeout == 0) {
        req->timeout = cli->timeout;
    }

    for (const auto& pair : cli->headers) {
        if (req->headers.find(pair.first) == req->headers.end()) {
            req->headers.insert(pair);
        }
    }

    return 0;
}

int http_client_send(http_client_t* cli, HttpRequest* req, HttpResponse* resp) {
    if (!cli || !req || !resp) return ERR_NULL_POINTER;

    http_client_make_request(cli, req);

    if (req->http_cb) resp->http_cb = std::move(req->http_cb);

    int ret = __http_client_send(cli, req, resp);
    if (ret != 0) return ret;

    // redirect
    if (req->redirect && HTTP_STATUS_IS_REDIRECT(resp->status_code)) {
        return http_client_redirect(req, resp);
    }
    return 0;
}

int http_client_send(HttpRequest* req, HttpResponse* resp) {
    if (!req || !resp) return ERR_NULL_POINTER;

    http_client_t cli;
    return http_client_send(&cli, req, resp);
}

#ifdef WITH_CURL
static size_t s_header_cb(char* buf, size_t size, size_t cnt, void* userdata) {
    if (buf == NULL || userdata == NULL)    return 0;
    size_t len = size * cnt;
    std::string str(buf, len);
    HttpResponse* resp = (HttpResponse*)userdata;
    std::string::size_type pos = str.find_first_of(':');
    if (pos == std::string::npos) {
        if (strncmp(buf, "HTTP/", 5) == 0) {
            // status line
            //hlogd("%s", buf);
            int http_major,http_minor,status_code;
            if (buf[5] == '1') {
                // HTTP/1.1 200 OK\r\n
                sscanf(buf, "HTTP/%d.%d %d", &http_major, &http_minor, &status_code);
            }
            else if (buf[5] == '2') {
                // HTTP/2 200\r\n
                sscanf(buf, "HTTP/%d %d", &http_major, &status_code);
                http_minor = 0;
            }
            resp->http_major = http_major;
            resp->http_minor = http_minor;
            resp->status_code = (http_status)status_code;
            if (resp->http_cb) {
                resp->http_cb(resp, HP_MESSAGE_BEGIN, NULL, 0);
            }
        }
    }
    else {
        // headers
        std::string key = trim(str.substr(0, pos));
        std::string value = trim(str.substr(pos+1));
        resp->headers[key] = value;
    }
    return len;
}

static size_t s_body_cb(char* buf, size_t size, size_t cnt, void *userdata) {
    if (buf == NULL || userdata == NULL)    return 0;
    size_t len = size * cnt;
    HttpMessage* resp = (HttpMessage*)userdata;
    if (resp->http_cb) {
        if (resp->content == NULL && resp->content_length == 0) {
            resp->content = buf;
            resp->content_length = len;
            resp->http_cb(resp, HP_HEADERS_COMPLETE, NULL, 0);
        }
        resp->http_cb(resp, HP_BODY, buf, len);
    } else {
        resp->body.append(buf, len);
    }
    return len;
}

int __http_client_send(http_client_t* cli, HttpRequest* req, HttpResponse* resp) {
    if (cli->curl == NULL) {
        cli->curl = curl_easy_init();
    }
    CURL* curl = cli->curl;

    // proxy
    if (req->IsProxy()) {
        curl_easy_setopt(curl, CURLOPT_PROXY, req->host.c_str());
        curl_easy_setopt(curl, CURLOPT_PROXYPORT, req->port);
    }

    // SSL
    if (req->IsHttps()) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);
    }

    // http2
    if (req->http_major == 2) {
#if LIBCURL_VERSION_NUM < 0x073100 // 7.49.0
        curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_2_0);
#else
        // No Connection: Upgrade
        curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_2_PRIOR_KNOWLEDGE);
#endif
    }

    // TCP_NODELAY
    curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1);

    // method
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, http_method_str(req->method));

    // url
    req->DumpUrl();
    curl_easy_setopt(curl, CURLOPT_URL, req->url.c_str());
    //hlogd("%s %s HTTP/%d.%d", http_method_str(req->method), req->url.c_str(), req->http_major, req->http_minor);

    // headers
    req->FillContentType();
    struct curl_slist *headers = NULL;
    for (auto& pair : req->headers) {
        std::string header = pair.first;
        header += ": ";
        header += pair.second;
        headers = curl_slist_append(headers, header.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // body
    //struct curl_httppost* httppost = NULL;
    //struct curl_httppost* lastpost = NULL;
    if (req->body.size() == 0) {
        req->DumpBody();
        /*
        if (req->body.size() == 0 &&
            req->content_type == MULTIPART_FORM_DATA) {
            for (auto& pair : req->mp) {
                if (pair.second.filename.size() != 0) {
                    curl_formadd(&httppost, &lastpost,
                            CURLFORM_COPYNAME, pair.first.c_str(),
                            CURLFORM_FILE, pair.second.filename.c_str(),
                            CURLFORM_END);
                }
                else if (pair.second.content.size() != 0) {
                    curl_formadd(&httppost, &lastpost,
                            CURLFORM_COPYNAME, pair.first.c_str(),
                            CURLFORM_COPYCONTENTS, pair.second.content.c_str(),
                            CURLFORM_END);
                }
            }
            if (httppost) {
                curl_easy_setopt(curl, CURLOPT_HTTPPOST, httppost);
            }
        }
        */
    }
    if (req->body.size() != 0) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req->body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, req->body.size());
    }

    if (req->connect_timeout > 0) {
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, req->connect_timeout);
    }
    if (req->timeout > 0) {
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, req->timeout);
    }

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, s_body_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, resp);

    curl_easy_setopt(curl, CURLOPT_HEADER, 0);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, s_header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, resp);

    int ret = curl_easy_perform(curl);
    /*
    if (ret != 0) {
        hloge("curl error: %d: %s", ret, curl_easy_strerror((CURLcode)ret));
    }
    if (resp->body.length() != 0) {
        hlogd("[Response]\n%s", resp->body.c_str());
    }
    double total_time, name_time, conn_time, pre_time;
    curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &total_time);
    curl_easy_getinfo(curl, CURLINFO_NAMELOOKUP_TIME, &name_time);
    curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME, &conn_time);
    curl_easy_getinfo(curl, CURLINFO_PRETRANSFER_TIME, &pre_time);
    hlogd("TIME_INFO: %lf,%lf,%lf,%lf", total_time, name_time, conn_time, pre_time);
    */

    if (headers) {
        curl_slist_free_all(headers);
    }
    /*
    if (httppost) {
        curl_formfree(httppost);
    }
    */

    if (resp->http_cb) {
        resp->http_cb(resp, HP_MESSAGE_COMPLETE, NULL, 0);
    }

    return ret;
}

const char* http_client_strerror(int errcode) {
    return curl_easy_strerror((CURLcode)errcode);
}
#else
static int http_client_connect(http_client_t* cli, const char* host, int port, int https, int timeout) {
    int blocktime = DEFAULT_CONNECT_TIMEOUT;
    if (timeout > 0) {
        blocktime = MIN(timeout*1000, blocktime);
    }
    int connfd = ConnectTimeout(host, port, blocktime);
    if (connfd < 0) {
        fprintf(stderr, "* connect %s:%d failed!\n", host, port);
        hloge("connect %s:%d failed!", host, port);
        return connfd;
    }
    tcp_nodelay(connfd, 1);

    if (https && cli->ssl == NULL) {
        // cli->ssl_ctx > g_ssl_ctx > hssl_ctx_new
        hssl_ctx_t ssl_ctx = NULL;
        if (cli->ssl_ctx) {
            ssl_ctx = cli->ssl_ctx;
        } else if (g_ssl_ctx) {
            ssl_ctx = g_ssl_ctx;
        } else {
            cli->ssl_ctx = ssl_ctx = hssl_ctx_new(NULL);
            cli->alloced_ssl_ctx = true;
        }
        if (ssl_ctx == NULL) {
            closesocket(connfd);
            return NABS(ERR_NEW_SSL_CTX);
        }
        cli->ssl = hssl_new(ssl_ctx, connfd);
        if (cli->ssl == NULL) {
            closesocket(connfd);
            return NABS(ERR_NEW_SSL);
        }
        if (!is_ipaddr(host)) {
            hssl_set_sni_hostname(cli->ssl, host);
        }
        int ret = hssl_connect(cli->ssl);
        if (ret != 0) {
            fprintf(stderr, "* ssl handshake failed: %d\n", ret);
            hloge("ssl handshake failed: %d", ret);
            hssl_free(cli->ssl);
            cli->ssl = NULL;
            closesocket(connfd);
            return NABS(ret);
        }
    }

    cli->fd = connfd;
    return connfd;
}

int __http_client_send(http_client_t* cli, HttpRequest* req, HttpResponse* resp) {
    // connect -> send -> recv -> http_parser
    int err = 0;
    int timeout = req->timeout;
    int connfd = cli->fd;
    bool https = req->IsHttps() && !req->IsProxy();
    bool keepalive = true;

    time_t start_time = time(NULL);
    time_t cur_time;
    int fail_cnt = 0;
    if (connfd <= 0) {
connect:
        connfd = http_client_connect(cli, req->host.c_str(), req->port, https, MIN(req->connect_timeout, req->timeout));
        if (connfd < 0) {
            return connfd;
        }
    }

    if (cli->parser == NULL) {
        cli->parser = HttpParserPtr(HttpParser::New(HTTP_CLIENT, (http_version)req->http_major));
    }
    cli->parser->SubmitRequest(req);
    char recvbuf[1024] = {0};
    int total_nsend, nsend, nrecv;
    total_nsend = nsend = nrecv = 0;
send:
    char* data = NULL;
    size_t len  = 0;
    while (cli->parser->GetSendData(&data, &len)) {
        total_nsend = 0;
        while (total_nsend < len) {
            if (timeout > 0) {
                cur_time = time(NULL);
                if (cur_time - start_time >= timeout) {
                    return ERR_TASK_TIMEOUT;
                }
                so_sndtimeo(connfd, (timeout-(cur_time-start_time)) * 1000);
            }
            if (https) {
                nsend = hssl_write(cli->ssl, data+total_nsend, len-total_nsend);
            }
            else {
                nsend = send(connfd, data+total_nsend, len-total_nsend, 0);
            }
            if (nsend <= 0) {
                if (++fail_cnt == 1) {
                    // maybe keep-alive timeout, try again
                    cli->Close();
                    goto connect;
                }
                else {
                    err = socket_errno();
                    goto disconnect;
                }
            }
            total_nsend += nsend;
        }
    }
    cli->parser->InitResponse(resp);
recv:
    do {
        if (timeout > 0) {
            cur_time = time(NULL);
            if (cur_time - start_time >= timeout) {
                return ERR_TASK_TIMEOUT;
            }
            so_rcvtimeo(connfd, (timeout-(cur_time-start_time)) * 1000);
        }
        if (https) {
            nrecv = hssl_read(cli->ssl, recvbuf, sizeof(recvbuf));
        }
        else {
            nrecv = recv(connfd, recvbuf, sizeof(recvbuf), 0);
        }
        if (nrecv <= 0) {
            if (resp->content_length == 0 && resp->http_major == 1 && resp->http_minor == 0) {
                // HTTP/1.0, assume close after body
                goto disconnect;
            }
            if (++fail_cnt == 1) {
                // maybe keep-alive timeout, try again
                cli->Close();
                goto connect;
            }
            else {
                err = socket_errno();
                goto disconnect;
            }
        }
        int nparse = cli->parser->FeedRecvData(recvbuf, nrecv);
        if (nparse != nrecv) {
            return ERR_PARSE;
        }
    } while(!cli->parser->IsComplete());

    keepalive = req->IsKeepAlive() && resp->IsKeepAlive();
    if (!keepalive) {
        cli->Close();
    }
    return 0;
disconnect:
    cli->Close();
    return err;
}

const char* http_client_strerror(int errcode) {
    return socket_strerror(errcode);
}
#endif

static int __http_client_send_async(http_client_t* cli, HttpRequestPtr req, HttpResponseCallback resp_cb) {
    if (cli->async_client_ == NULL) {
        cli->mutex_.lock();
        if (cli->async_client_ == NULL) {
            cli->async_client_.reset(new hv::AsyncHttpClient);
        }
        cli->mutex_.unlock();
    }

    return cli->async_client_->send(req, std::move(resp_cb));
}

int http_client_send_async(http_client_t* cli, HttpRequestPtr req, HttpResponseCallback resp_cb) {
    if (!cli || !req) return ERR_NULL_POINTER;
    http_client_make_request(cli, req.get());
    return __http_client_send_async(cli, req, std::move(resp_cb));
}

static http_client_t* __get_default_async_client();
static void           __del_default_async_client();
http_client_t* __get_default_async_client() {
    static http_client_t* s_default_async_client = NULL;
    static std::mutex     s_mutex;
    if (s_default_async_client == NULL) {
        s_mutex.lock();
        if (s_default_async_client == NULL) {
            hlogi("create default http async client");
            s_default_async_client = http_client_new();
            // NOTE: I have No better idea
            atexit(__del_default_async_client);
        }
        s_mutex.unlock();
    }
    return s_default_async_client;
}
void __del_default_async_client() {
    hlogi("destory default http async client");
    http_client_del(__get_default_async_client());
}

int http_client_send_async(HttpRequestPtr req, HttpResponseCallback resp_cb) {
    if (req == NULL) return ERR_NULL_POINTER;

    if (req->timeout == 0) {
        req->timeout = DEFAULT_HTTP_TIMEOUT;
    }

    return __http_client_send_async(__get_default_async_client(), req, std::move(resp_cb));
}
