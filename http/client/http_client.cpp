#include "http_client.h"

#include "hstring.h"

#ifdef WITH_CURL

/***************************************************************
HttpClient based libcurl
***************************************************************/
#include "curl/curl.h"

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

int http_client_send(HttpRequest* req, HttpResponse* res, int timeout) {
    if (req == NULL || res == NULL) {
        return -1;
    }

    CURL* handle = curl_easy_init();

    // SSL
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 0);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 0);

    // TCP_NODELAY
    curl_easy_setopt(handle, CURLOPT_TCP_NODELAY, 1);

    // method
    curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, http_method_str(req->method));

    // url
    std::string url = req->dump_url();
    curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
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
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);

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
                curl_easy_setopt(handle, CURLOPT_HTTPPOST, httppost);
                curl_formget(httppost, NULL, s_formget_cb);
            }
        }
    }
    if (req->body.size() != 0) {
        curl_easy_setopt(handle, CURLOPT_POSTFIELDS, req->body.c_str());
        curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, req->body.size());
    }

    if (timeout > 0) {
        curl_easy_setopt(handle, CURLOPT_TIMEOUT, timeout);
    }

    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, s_body_cb);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, res);

    curl_easy_setopt(handle, CURLOPT_HEADER, 0);
    curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, s_header_cb);
    curl_easy_setopt(handle, CURLOPT_HEADERDATA, res);

    int ret = curl_easy_perform(handle);
    /*
    if (ret != 0) {
        hloge("curl error: %d: %s", ret, curl_easy_strerror((CURLcode)ret));
    }
    if (res->body.length() != 0) {
        hlogd("[Response]\n%s", res->body.c_str());
    }
    double total_time, name_time, conn_time, pre_time;
    curl_easy_getinfo(handle, CURLINFO_TOTAL_TIME, &total_time);
    curl_easy_getinfo(handle, CURLINFO_NAMELOOKUP_TIME, &name_time);
    curl_easy_getinfo(handle, CURLINFO_CONNECT_TIME, &conn_time);
    curl_easy_getinfo(handle, CURLINFO_PRETRANSFER_TIME, &pre_time);
    hlogd("TIME_INFO: %lf,%lf,%lf,%lf", total_time, name_time, conn_time, pre_time);
    */

    if (headers) {
        curl_slist_free_all(headers);
    }
    if (httppost) {
        curl_formfree(httppost);
    }

    curl_easy_cleanup(handle);

    return ret;
}

const char* http_client_strerror(int errcode) {
    return curl_easy_strerror((CURLcode)errcode);
}
#else
#include "herr.h"
#include "hsocket.h"
#include "HttpParser.h"
#include "ssl_ctx.h"
#ifdef WITH_OPENSSL
#include "openssl/ssl.h"
#endif
int http_client_send(HttpRequest* req, HttpResponse* res, int timeout) {
    // connect -> send -> recv -> http_parser
    int ssl_enable = 0;
    if (strncmp(req->url.c_str(), "https", 5) == 0) {
        ssl_enable = 1;
#ifdef WITH_OPENSSL
        if (g_ssl_ctx == NULL) {
            ssl_ctx_init(NULL, NULL, NULL);
        }
#else
        fprintf(stderr, "Please recompile WITH_OPENSSL\n");
        return ERR_INVALID_PROTOCOL;
#endif
    }
    time_t start_time = time(NULL);
    time_t cur_time;
    std::string http = req->dump(true, true);
    auto Host = req->headers.find("Host");
    if (Host == req->headers.end()) {
        return ERR_INVALID_PARAM;
    }
    StringList strlist = split(Host->second, ':');
    std::string host;
    int port = 80;
    host = strlist[0];
    if (strlist.size() == 2) {
        port = atoi(strlist[1].c_str());
    }
    int blocktime = 3000;
    if (timeout > 0) {
        blocktime = MIN(timeout*1000, blocktime);
    }
    SOCKET connfd = ConnectTimeout(host.c_str(), port, blocktime);
    if (connfd < 0) {
        return socket_errno();
    }
#ifdef WITH_OPENSSL
    SSL* ssl = NULL;
    if (ssl_enable) {
        ssl = SSL_new((SSL_CTX*)g_ssl_ctx);
        SSL_set_fd(ssl, connfd);
        if (SSL_connect(ssl) != 1) {
            fprintf(stderr, "SSL handshark failed: %d\n", SSL_get_error(ssl, -1));
        }
    }
#endif
    tcp_nodelay(connfd, 1);
    int err = 0;
    HttpParser parser;
    parser.parser_response_init(res);
    char recvbuf[1024] = {0};
send:
    int total_nsend = 0;
    int nsend = 0;
    int nrecv = 0;
    while (1) {
        if (timeout > 0) {
            cur_time = time(NULL);
            if (cur_time - start_time >= timeout) {
                err = ERR_TASK_TIMEOUT;
                goto ret;
            }
            so_sndtimeo(connfd, (timeout-(cur_time-start_time)) * 1000);
        }
#ifdef WITH_OPENSSL
        if (ssl_enable) {
            nsend = SSL_write(ssl, http.c_str()+total_nsend, http.size()-total_nsend);
        }
#endif
        if (!ssl_enable) {
            nsend = send(connfd, http.c_str()+total_nsend, http.size()-total_nsend, 0);
        }
        if (nsend <= 0) {
            err = socket_errno();
            goto ret;
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
                err = ERR_TASK_TIMEOUT;
                goto ret;
            }
            so_rcvtimeo(connfd, (timeout-(cur_time-start_time)) * 1000);
        }
#ifdef WITH_OPENSSL
        if (ssl_enable) {
            nrecv = SSL_read(ssl, recvbuf, sizeof(recvbuf));
        }
#endif
        if (!ssl_enable) {
            nrecv = recv(connfd, recvbuf, sizeof(recvbuf), 0);
        }
        if (nrecv <= 0) {
            err = socket_errno();
            goto ret;
        }
        int nparse = parser.execute(recvbuf, nrecv);
        if (nparse != nrecv || parser.get_errno() != HPE_OK) {
            err = ERR_PARSE;
            goto ret;
        }
        if (parser.get_state() == HP_MESSAGE_COMPLETE) {
            err = 0;
            break;
        }
        if (timeout > 0) {
            cur_time = time(NULL);
            if (cur_time - start_time >= timeout) {
                err = ERR_TASK_TIMEOUT;
                goto ret;
            }
            so_rcvtimeo(connfd, (timeout-(cur_time-start_time)) * 1000);
        }
    }
ret:
#ifdef WITH_OPENSSL
    if (ssl) {
        SSL_free(ssl);
    }
#endif
    closesocket(connfd);
    return err;
}

const char* http_client_strerror(int errcode) {
    return socket_strerror(errcode);
}
#endif
