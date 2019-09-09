#include "http_client.h"

#include "hstring.h"

#define MAX_CONNECT_TIMEOUT 3000 // ms

#ifdef WITH_CURL
#include "curl/curl.h"
#else
#include "herr.h"
#include "hsocket.h"
#include "HttpParser.h"
#include "ssl_ctx.h"
#endif

#ifdef WITH_OPENSSL
#include "openssl/ssl.h"
#endif

struct http_session_s {
    int          use_tls;
    std::string  host;
    int          port;
    int          timeout;
    http_headers headers;
//private:
#ifdef WITH_CURL
    CURL* curl;
#else
    int fd;
#endif
#ifdef WITH_OPENSSL
    SSL* ssl;
#endif

    http_session_s() {
        use_tls = 0;
        port = DEFAULT_HTTP_PORT;
        timeout = DEFAULT_HTTP_TIMEOUT;
#ifdef WITH_CURL
        curl = NULL;
#else
        fd = -1;
#endif
#ifdef WITH_OPENSSL
        ssl = NULL;
#endif
    }

    ~http_session_s() {
#ifdef WITH_OPENSSL
        if (ssl) {
            SSL_free(ssl);
            ssl = NULL;
        }
#endif
#ifdef WITH_CURL
        if (curl) {
            curl_easy_cleanup(curl);
            curl = NULL;
        }
#else
        if (fd > 0) {
            closesocket(fd);
            fd = -1;
        }
#endif
    }
};

static int __http_session_send(http_session_t* hss, HttpRequest* req, HttpResponse* res);

http_session_t* http_session_new(const char* host, int port) {
    http_session_t* hss = new http_session_t;
    hss->host = host;
    hss->port = port;
    hss->headers["Host"] = asprintf("%s:%d", host, port);
    hss->headers["Connection"] = "keep-alive";
    return hss;
}

int http_session_del(http_session_t* hss) {
    if (hss == NULL) return 0;
    delete hss;
    return 0;
}

int http_session_set_timeout(http_session_t* hss, int timeout) {
    hss->timeout = timeout;
    return 0;
}

int http_session_clear_headers(http_session_t* hss) {
    hss->headers.clear();
    return 0;
}

int http_session_set_header(http_session_t* hss, const char* key, const char* value) {
    hss->headers[key] = value;
    return 0;
}

int http_session_del_header(http_session_t* hss, const char* key) {
    auto iter = hss->headers.find(key);
    if (iter != hss->headers.end()) {
        hss->headers.erase(iter);
    }
    return 0;
}

const char* http_session_get_header(http_session_t* hss, const char* key) {
    auto iter = hss->headers.find(key);
    if (iter != hss->headers.end()) {
        return iter->second.c_str();
    }
    return NULL;
}

int http_session_send(http_session_t* hss, HttpRequest* req, HttpResponse* res) {
    for (auto& pair : hss->headers) {
        req->headers[pair.first] = pair.second;
    }
    return __http_session_send(hss, req, res);
}

int http_client_send(HttpRequest* req, HttpResponse* res, int timeout) {
    http_session_t hss;
    hss.timeout = timeout;
    return __http_session_send(&hss, req, res);
}

#ifdef WITH_CURL

static size_t s_formget_cb(void *arg, const char *buf, size_t len) {
    return len;
}

static size_t s_header_cb(char* buf, size_t size, size_t cnt, void* userdata) {
    if (buf == NULL || userdata == NULL)    return 0;

    HttpResponse* res = (HttpResponse*)userdata;

    string str(buf);
    string::size_type pos = str.find_first_of(':');
    if (pos == string::npos) {
        if (strncmp(buf, "HTTP/", 5) == 0) {
            // status line
            // HTTP/1.1 200 OK\r\n
            //hlogd("%s", buf);
            int http_major,http_minor,status_code;
            sscanf(buf, "HTTP/%d.%d %d", &http_major, &http_minor, &status_code);
            res->http_major = http_major;
            res->http_minor = http_minor;
            res->status_code = (http_status)status_code;
        }
    }
    else {
        // headers
        string key = trim(str.substr(0, pos));
        string value = trim(str.substr(pos+1));
        res->headers[key] = value;
    }
    return size*cnt;
}

static size_t s_body_cb(char *buf, size_t size, size_t cnt, void *userdata) {
    if (buf == NULL || userdata == NULL)    return 0;

    HttpResponse* res = (HttpResponse*)userdata;
    res->body.append(buf, size*cnt);
    return size*cnt;
}

int __http_session_send(http_session_t* hss, HttpRequest* req, HttpResponse* res) {
    if (req == NULL || res == NULL) {
        return -1;
    }

    if (hss->curl == NULL) {
        hss->curl = curl_easy_init();
    }
    CURL* curl = hss->curl;
    int timeout = hss->timeout;

    // SSL
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);

    // TCP_NODELAY
    curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1);

    // method
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, http_method_str(req->method));

    // url
    std::string url = req->dump_url();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    //hlogd("%s %s HTTP/%d.%d", http_method_str(req->method), url.c_str(), req->http_major, req->http_minor);

    // header
    req->fill_content_type();
    struct curl_slist *headers = NULL;
    for (auto& pair : req->headers) {
        string header = pair.first;
        header += ": ";
        header += pair.second;
        headers = curl_slist_append(headers, header.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // body
    struct curl_httppost* httppost = NULL;
    struct curl_httppost* lastpost = NULL;
    if (req->body.size() == 0) {
        req->dump_body();
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
                curl_formget(httppost, NULL, s_formget_cb);
            }
        }
    }
    if (req->body.size() != 0) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req->body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, req->body.size());
    }

    if (timeout > 0) {
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
    }

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, s_body_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, res);

    curl_easy_setopt(curl, CURLOPT_HEADER, 0);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, s_header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, res);

    int ret = curl_easy_perform(curl);
    /*
    if (ret != 0) {
        hloge("curl error: %d: %s", ret, curl_easy_strerror((CURLcode)ret));
    }
    if (res->body.length() != 0) {
        hlogd("[Response]\n%s", res->body.c_str());
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
    if (httppost) {
        curl_formfree(httppost);
    }

    return ret;
}

const char* http_client_strerror(int errcode) {
    return curl_easy_strerror((CURLcode)errcode);
}
#else
static int __http_session_connect(http_session_t* hss) {
    int blocktime = MAX_CONNECT_TIMEOUT;
    if (hss->timeout > 0) {
        blocktime = MIN(hss->timeout*1000, blocktime);
    }
    int connfd = ConnectTimeout(hss->host.c_str(), hss->port, blocktime);
    if (connfd < 0) {
        return socket_errno();
    }
    tcp_nodelay(connfd, 1);

    if (hss->use_tls) {
#ifdef WITH_OPENSSL
        if (g_ssl_ctx == NULL) {
            ssl_ctx_init(NULL, NULL, NULL);
        }
        hss->ssl = SSL_new((SSL_CTX*)g_ssl_ctx);
        SSL_set_fd(hss->ssl, connfd);
        if (SSL_connect(hss->ssl) != 1) {
            int err = SSL_get_error(hss->ssl, -1);
            fprintf(stderr, "SSL handshark failed: %d\n", err);
            SSL_free(hss->ssl);
            hss->ssl = NULL;
            closesocket(connfd);
            return err;
        }
#else
        fprintf(stderr, "Please recompile WITH_OPENSSL\n");
        closesocket(connfd);
        return ERR_INVALID_PROTOCOL;
#endif
    }
    hss->fd = connfd;
    return 0;
}

static int __http_session_close(http_session_t* hss) {
#ifdef WITH_OPENSSL
    if (hss->ssl) {
        SSL_free(hss->ssl);
        hss->ssl = NULL;
    }
#endif
    if (hss->fd > 0) {
        closesocket(hss->fd);
        hss->fd = -1;
    }
    return 0;
}

static int __http_session_send(http_session_t* hss, HttpRequest* req, HttpResponse* res) {
    // connect -> send -> recv -> http_parser
    int err = 0;
    int timeout = hss->timeout;
    int connfd = hss->fd;

    // use_tls ?
    int use_tls = hss->use_tls;
    if (strncmp(req->url.c_str(), "https", 5) == 0) {
        hss->use_tls = use_tls = 1;
    }

    // parse host:port from Headers
    std::string http = req->dump(true, true);
    if (hss->host.size() == 0) {
        auto Host = req->headers.find("Host");
        if (Host == req->headers.end()) {
            return ERR_INVALID_PARAM;
        }
        StringList strlist = split(Host->second, ':');
        hss->host = strlist[0];
        if (strlist.size() == 2) {
            hss->port = atoi(strlist[1].c_str());
        }
        else {
            hss->port = DEFAULT_HTTP_PORT;
        }
    }

    time_t start_time = time(NULL);
    time_t cur_time;
    int fail_cnt = 0;
connect:
    if (connfd <= 0) {
        int ret = __http_session_connect(hss);
        if (ret != 0) {
            return ret;
        }
        connfd = hss->fd;
    }

    HttpParser parser;
    parser.parser_response_init(res);
    char recvbuf[1024] = {0};
    int total_nsend, nsend, nrecv;
send:
    total_nsend = nsend = nrecv = 0;
    while (1) {
        if (timeout > 0) {
            cur_time = time(NULL);
            if (cur_time - start_time >= timeout) {
                return ERR_TASK_TIMEOUT;
            }
            so_sndtimeo(connfd, (timeout-(cur_time-start_time)) * 1000);
        }
#ifdef WITH_OPENSSL
        if (use_tls) {
            nsend = SSL_write(hss->ssl, http.c_str()+total_nsend, http.size()-total_nsend);
        }
#endif
        if (!use_tls) {
            nsend = send(connfd, http.c_str()+total_nsend, http.size()-total_nsend, 0);
        }
        if (nsend <= 0) {
            if (++fail_cnt == 1) {
                // maybe keep-alive timeout, try again
                __http_session_close(hss);
                goto connect;
            }
            else {
                return socket_errno();
            }
        }
        total_nsend += nsend;
        if (total_nsend == http.size()) {
            break;
        }
    }
recv:
    while(1) {
        if (timeout > 0) {
            cur_time = time(NULL);
            if (cur_time - start_time >= timeout) {
                return ERR_TASK_TIMEOUT;
            }
            so_rcvtimeo(connfd, (timeout-(cur_time-start_time)) * 1000);
        }
#ifdef WITH_OPENSSL
        if (use_tls) {
            nrecv = SSL_read(hss->ssl, recvbuf, sizeof(recvbuf));
        }
#endif
        if (!use_tls) {
            nrecv = recv(connfd, recvbuf, sizeof(recvbuf), 0);
        }
        if (nrecv <= 0) {
            return socket_errno();
        }
        int nparse = parser.execute(recvbuf, nrecv);
        if (nparse != nrecv || parser.get_errno() != HPE_OK) {
            return ERR_PARSE;
        }
        if (parser.get_state() == HP_MESSAGE_COMPLETE) {
            err = 0;
            break;
        }
        if (timeout > 0) {
            cur_time = time(NULL);
            if (cur_time - start_time >= timeout) {
                return ERR_TASK_TIMEOUT;
            }
            so_rcvtimeo(connfd, (timeout-(cur_time-start_time)) * 1000);
        }
    }
    return err;
}

const char* http_client_strerror(int errcode) {
    return socket_strerror(errcode);
}
#endif
