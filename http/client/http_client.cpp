#include "http_client.h"

#define MAX_CONNECT_TIMEOUT 3000 // ms

#include "hstring.h" // import asprintf,trim

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

struct http_client_s {
    std::string  host;
    int          port;
    int          tls;
    int          http_version;
    int          timeout; // s
    http_headers headers;
//private:
#ifdef WITH_CURL
    CURL* curl;
#else
    int fd;
    HttpParser*  parser;
#endif
#ifdef WITH_OPENSSL
    SSL* ssl;
#endif

    http_client_s() {
        port = DEFAULT_HTTP_PORT;
        tls = 0;
        http_version = 1;
        timeout = DEFAULT_HTTP_TIMEOUT;
#ifdef WITH_CURL
        curl = NULL;
#else
        fd = -1;
        parser = NULL;
#endif
#ifdef WITH_OPENSSL
        ssl = NULL;
#endif
    }

    ~http_client_s() {
        Close();
    }

    void Close() {
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
        if (parser) {
            delete parser;
            parser = NULL;
        }
#endif
    }
};

static int __http_client_send(http_client_t* cli, HttpRequest* req, HttpResponse* res);

http_client_t* http_client_new(const char* host, int port, int tls) {
    http_client_t* cli = new http_client_t;
    cli->tls = tls;
    cli->port = port;
    if (host) {
        cli->host = host;
        cli->headers["Host"] = asprintf("%s:%d", host, port);
    }
    cli->headers["Connection"] = "keep-alive";
    return cli;
}

int http_client_del(http_client_t* cli) {
    if (cli == NULL) return 0;
    delete cli;
    return 0;
}

int http_client_set_timeout(http_client_t* cli, int timeout) {
    cli->timeout = timeout;
    return 0;
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

int http_client_send(http_client_t* cli, HttpRequest* req, HttpResponse* res) {
    for (auto& pair : cli->headers) {
        if (req->headers.find(pair.first) == req->headers.end()) {
            req->headers[pair.first] = pair.second;
        }
    }
    return __http_client_send(cli, req, res);
}

int http_client_send(HttpRequest* req, HttpResponse* res, int timeout) {
    http_client_t cli;
    cli.timeout = timeout;
    return __http_client_send(&cli, req, res);
}

#ifdef WITH_CURL
static size_t s_header_cb(char* buf, size_t size, size_t cnt, void* userdata) {
    if (buf == NULL || userdata == NULL)    return 0;

    HttpResponse* res = (HttpResponse*)userdata;

    std::string str(buf);
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
            res->http_major = http_major;
            res->http_minor = http_minor;
            res->status_code = (http_status)status_code;
        }
    }
    else {
        // headers
        std::string key = trim(str.substr(0, pos));
        std::string value = trim(str.substr(pos+1));
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

int __http_client_send(http_client_t* cli, HttpRequest* req, HttpResponse* res) {
    if (req == NULL || res == NULL) {
        return -1;
    }

    if (cli->curl == NULL) {
        cli->curl = curl_easy_init();
    }
    CURL* curl = cli->curl;

    // SSL
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);

    // http2
    if (req->http_major == 2) {
        //curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_2_0);
        //No Connection: Upgrade
        curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_2_PRIOR_KNOWLEDGE);
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

    if (cli->timeout > 0) {
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, cli->timeout);
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
    /*
    if (httppost) {
        curl_formfree(httppost);
    }
    */

    return ret;
}

const char* http_client_strerror(int errcode) {
    return curl_easy_strerror((CURLcode)errcode);
}
#else
static int __http_client_connect(http_client_t* cli) {
    int blocktime = MAX_CONNECT_TIMEOUT;
    if (cli->timeout > 0) {
        blocktime = MIN(cli->timeout*1000, blocktime);
    }
    int connfd = ConnectTimeout(cli->host.c_str(), cli->port, blocktime);
    if (connfd < 0) {
        return connfd;
    }
    tcp_nodelay(connfd, 1);

    if (cli->tls) {
#ifdef WITH_OPENSSL
        if (ssl_ctx_instance() == NULL) {
            ssl_ctx_init(NULL, NULL, NULL);
        }
        cli->ssl = SSL_new((SSL_CTX*)ssl_ctx_instance());
        SSL_set_fd(cli->ssl, connfd);
        if (SSL_connect(cli->ssl) != 1) {
            int err = SSL_get_error(cli->ssl, -1);
            fprintf(stderr, "SSL handshark failed: %d\n", err);
            SSL_free(cli->ssl);
            cli->ssl = NULL;
            closesocket(connfd);
            return err;
        }
#else
        fprintf(stderr, "Please recompile WITH_OPENSSL\n");
        closesocket(connfd);
        return ERR_INVALID_PROTOCOL;
#endif
    }

    if (cli->parser == NULL) {
        cli->parser = HttpParser::New(HTTP_CLIENT, (http_version)cli->http_version);
    }

    cli->fd = connfd;
    return 0;
}

int __http_client_send(http_client_t* cli, HttpRequest* req, HttpResponse* res) {
    // connect -> send -> recv -> http_parser
    int err = 0;
    int timeout = cli->timeout;
    int connfd = cli->fd;

    req->ParseUrl();
    if (cli->host.size() == 0) {
        cli->host = req->host;
        cli->port = req->port;
    }
    if (cli->tls == 0) {
        cli->tls = req->https;
    }
    cli->http_version = req->http_major;

    time_t start_time = time(NULL);
    time_t cur_time;
    int fail_cnt = 0;
connect:
    if (connfd <= 0) {
        int ret = __http_client_connect(cli);
        if (ret != 0) {
            return ret;
        }
        connfd = cli->fd;
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
        while (1) {
            if (timeout > 0) {
                cur_time = time(NULL);
                if (cur_time - start_time >= timeout) {
                    return ERR_TASK_TIMEOUT;
                }
                so_sndtimeo(connfd, (timeout-(cur_time-start_time)) * 1000);
            }
#ifdef WITH_OPENSSL
            if (cli->tls) {
                nsend = SSL_write(cli->ssl, data+total_nsend, len-total_nsend);
            }
#endif
            if (!cli->tls) {
                nsend = send(connfd, data+total_nsend, len-total_nsend, 0);
            }
            if (nsend <= 0) {
                if (++fail_cnt == 1) {
                    // maybe keep-alive timeout, try again
                    cli->Close();
                    goto connect;
                }
                else {
                    return socket_errno();
                }
            }
            total_nsend += nsend;
            if (total_nsend == len) {
                break;
            }
        }
    }
    cli->parser->InitResponse(res);
recv:
    do {
        if (timeout > 0) {
            cur_time = time(NULL);
            if (cur_time - start_time >= timeout) {
                return ERR_TASK_TIMEOUT;
            }
            so_rcvtimeo(connfd, (timeout-(cur_time-start_time)) * 1000);
        }
#ifdef WITH_OPENSSL
        if (cli->tls) {
            nrecv = SSL_read(cli->ssl, recvbuf, sizeof(recvbuf));
        }
#endif
        if (!cli->tls) {
            nrecv = recv(connfd, recvbuf, sizeof(recvbuf), 0);
        }
        if (nrecv <= 0) {
            return socket_errno();
        }
        int nparse = cli->parser->FeedRecvData(recvbuf, nrecv);
        if (nparse != nrecv) {
            return ERR_PARSE;
        }
    } while(!cli->parser->IsComplete());
    return err;
}

const char* http_client_strerror(int errcode) {
    return socket_strerror(errcode);
}
#endif
