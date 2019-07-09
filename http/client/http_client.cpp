#include "http_client.h"

#include "hstring.h"

/***************************************************************
HttpClient based libcurl
***************************************************************/
#include "curl/curl.h"

//#include "hlog.h"

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

#include <atomic>
static std::atomic_flag s_curl_global_init(false);
int http_client_send(HttpRequest* req, HttpResponse* res, int timeout) {
    if (req == NULL || res == NULL) {
        return -1;
    }

    if (!s_curl_global_init.test_and_set()) {
        curl_global_init(CURL_GLOBAL_ALL);
    }

    CURL* handle = curl_easy_init();

    // SSL
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 0);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 0);

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
