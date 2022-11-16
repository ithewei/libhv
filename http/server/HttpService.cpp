#include "HttpService.h"

#include "hbase.h" // import hv_strendswith

namespace hv {

void HttpService::AddApi(const char* path, http_method method, const http_handler& handler) {
    std::shared_ptr<http_method_handlers> method_handlers = NULL;
    auto iter = api_handlers.find(path);
    if (iter == api_handlers.end()) {
        // add path
        method_handlers = std::make_shared<http_method_handlers>();
        api_handlers[path] = method_handlers;
    }
    else {
        method_handlers = iter->second;
    }
    for (auto iter = method_handlers->begin(); iter != method_handlers->end(); ++iter) {
        if (iter->method == method) {
            // update
            iter->handler = handler;
            return;
        }
    }
    // add
    method_handlers->push_back(http_method_handler(method, handler));
}

int HttpService::GetApi(const char* url, http_method method, http_handler** handler) {
    // {base_url}/path?query
    const char* s = url;
    const char* b = base_url.c_str();
    while (*s && *b && *s == *b) {++s;++b;}
    if (*b != '\0') {
        return HTTP_STATUS_NOT_FOUND;
    }
    const char* e = s;
    while (*e && *e != '?') ++e;

    std::string path = std::string(s, e);
    auto iter = api_handlers.find(path);
    if (iter == api_handlers.end()) {
        if (handler) *handler = NULL;
        return HTTP_STATUS_NOT_FOUND;
    }
    auto method_handlers = iter->second;
    for (auto iter = method_handlers->begin(); iter != method_handlers->end(); ++iter) {
        if (iter->method == method) {
            if (handler) *handler = &iter->handler;
            return 0;
        }
    }
    if (handler) *handler = NULL;
    return HTTP_STATUS_METHOD_NOT_ALLOWED;
}

int HttpService::GetApi(HttpRequest* req, http_handler** handler) {
    // {base_url}/path?query
    const char* s = req->path.c_str();
    const char* b = base_url.c_str();
    while (*s && *b && *s == *b) {++s;++b;}
    if (*b != '\0') {
        return HTTP_STATUS_NOT_FOUND;
    }
    const char* e = s;
    while (*e && *e != '?') ++e;

    std::string path = std::string(s, e);
    const char *kp, *ks, *vp, *vs;
    bool match;
    for (auto iter = api_handlers.begin(); iter != api_handlers.end(); ++iter) {
        kp = iter->first.c_str();
        vp = path.c_str();
        match = false;
        std::map<std::string, std::string> params;

        while (*kp && *vp) {
            if (kp[0] == '*') {
                // wildcard *
                match = hv_strendswith(vp, kp+1);
                break;
            } else if (*kp != *vp) {
                match = false;
                break;
            } else if (kp[0] == '/' && (kp[1] == ':' || kp[1] == '{')) {
                    // RESTful /:field/
                    // RESTful /{field}/
                    kp += 2;
                    ks = kp;
                    while (*kp && *kp != '/') {++kp;}
                    vp += 1;
                    vs = vp;
                    while (*vp && *vp != '/') {++vp;}
                    int klen = kp - ks;
                    if (*(ks-1) == '{' && *(kp-1) == '}') {
                        --klen;
                    }
                    params[std::string(ks, klen)] = std::string(vs, vp-vs);
                    continue;
            } else {
                ++kp;
                ++vp;
            }
        }

        match = match ? match : (*kp == '\0' && *vp == '\0');

        if (match) {
            auto method_handlers = iter->second;
            for (auto iter = method_handlers->begin(); iter != method_handlers->end(); ++iter) {
                if (iter->method == req->method) {
                    for (auto& param : params) {
                        // RESTful /:field/ => req->query_params[field]
                        req->query_params[param.first] = param.second;
                    }
                    if (handler) *handler = &iter->handler;
                    return 0;
                }
            }

            if (params.size() == 0) {
                if (handler) *handler = NULL;
                return HTTP_STATUS_METHOD_NOT_ALLOWED;
            }
        }
    }
    if (handler) *handler = NULL;
    return HTTP_STATUS_NOT_FOUND;
}

void HttpService::Static(const char* path, const char* dir) {
    std::string strPath(path);
    if (strPath.back() != '/') strPath += '/';
    std::string strDir(dir);
    if (strDir.back() == '/') strDir.pop_back();
    staticDirs[strPath] = strDir;
}

std::string HttpService::GetStaticFilepath(const char* path) {
    std::string filepath;
    for (auto iter = staticDirs.begin(); iter != staticDirs.end(); ++iter) {
        if (hv_strstartswith(path, iter->first.c_str())) {
            filepath = iter->second + (path + iter->first.length() - 1);
            break;
        }
    }

    if (filepath.empty()) {
        return filepath;
    }

    if (filepath.back() == '/') {
        filepath += home_page;
    }
    return filepath;
}

void HttpService::Proxy(const char* path, const char* url) {
    proxies[path] = url;
}

std::string HttpService::GetProxyUrl(const char* path) {
    std::string url;
    for (auto iter = proxies.begin(); iter != proxies.end(); ++iter) {
        if (hv_strstartswith(path, iter->first.c_str())) {
            url = iter->second + (path + iter->first.length());
            break;
        }
    }
    return url;
}

}
