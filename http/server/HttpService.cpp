#include "HttpService.h"

#include "hbase.h" // import strendswith

void HttpService::AddApi(const char* path, http_method method, http_sync_handler sync_handler, http_async_handler async_handler, http_handler handler) {
    std::shared_ptr<http_method_handlers> method_handlers = NULL;
    auto iter = api_handlers.find(path);
    if (iter == api_handlers.end()) {
        // add path
        method_handlers = std::shared_ptr<http_method_handlers>(new http_method_handlers);
        api_handlers[path] = method_handlers;
    }
    else {
        method_handlers = iter->second;
    }
    for (auto iter = method_handlers->begin(); iter != method_handlers->end(); ++iter) {
        if (iter->method == method) {
            // update
            iter->sync_handler = sync_handler;
            iter->async_handler = async_handler;
            iter->handler = handler;
            return;
        }
    }
    // add
    method_handlers->push_back(http_method_handler(method, sync_handler, async_handler, handler));
}

int HttpService::GetApi(const char* url, http_method method, http_sync_handler* sync_handler, http_async_handler* async_handler, http_handler* handler) {
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
        if (sync_handler) *sync_handler = NULL;
        if (async_handler) *async_handler = NULL;
        if (handler) *handler = NULL;
        return HTTP_STATUS_NOT_FOUND;
    }
    auto method_handlers = iter->second;
    for (auto iter = method_handlers->begin(); iter != method_handlers->end(); ++iter) {
        if (iter->method == method) {
            if (sync_handler) *sync_handler = iter->sync_handler;
            if (async_handler) *async_handler = iter->async_handler;
            if (handler) *handler = iter->handler;
            return 0;
        }
    }
    if (handler) *handler = NULL;
    if (async_handler) *async_handler = NULL;
    return HTTP_STATUS_METHOD_NOT_ALLOWED;
}

int HttpService::GetApi(HttpRequest* req, http_sync_handler* sync_handler, http_async_handler* async_handler, http_handler* handler) {
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
                match = strendswith(vp, kp+1);
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
                    if (sync_handler) *sync_handler = iter->sync_handler;
                    if (async_handler) *async_handler = iter->async_handler;
                    if (handler) *handler = iter->handler;
                    return 0;
                }
            }

            if (params.size() == 0) {
                if (sync_handler) *sync_handler = NULL;
                if (async_handler) *async_handler = NULL;
                if (handler) *handler = NULL;
                return HTTP_STATUS_METHOD_NOT_ALLOWED;
            }
        }
    }
    if (sync_handler) *sync_handler = NULL;
    if (async_handler) *async_handler = NULL;
    if (handler) *handler = NULL;
    return HTTP_STATUS_NOT_FOUND;
}
