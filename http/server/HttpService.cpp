#include "HttpService.h"
#include "HttpMiddleware.h"
#include "HttpRouter.h"

namespace hv {

void HttpService::AddRoute(const char* path, http_method method, const http_handler& handler) {
    if (!router) {
        router = std::make_shared<http_router>();
    }

    std::string route_path(path);
    http_method_handlers_ptr method_handlers;
    if (!router->Find(route_path, method_handlers)) {
        // new http_method_handlers
        method_handlers = std::make_shared<http_method_handlers>();
        router->Insert(route_path, method_handlers);
    }

    // insert handler into http_method_handlers
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

bool HttpService::HasRoutes() const {
    return router && !router->Empty();
}

hv::StringList HttpService::Paths() const {
    if (!HasRoutes()) {
        return hv::StringList();
    }
    return router->Paths();
}

int HttpService::GetRoute(const char* full_path, http_method method, http_handler** handler, std::map<std::string, std::string>& params) {
    if (handler) *handler = NULL;
    if (!HasRoutes()) {
        return HTTP_STATUS_NOT_FOUND;
    }

    // {base_url}/path?query
    const char* s = full_path;
    const char* b = base_url.c_str();
    while (*s && *b && *s == *b) {++s;++b;}
    if (*b != '\0') {
        return HTTP_STATUS_NOT_FOUND;
    }
    const char* e = s;
    while (*e && *e != '?') ++e;

    std::string path = std::string(s, e);
    if (path.empty()) {
        return HTTP_STATUS_NOT_FOUND;
    }

    http_method_handlers_ptr method_handlers;
    if (!router->Match(path, method_handlers, params)) {
        return HTTP_STATUS_NOT_FOUND;
    }
    for (auto iter = method_handlers->begin(); iter != method_handlers->end(); ++iter) {
        if (iter->method == method) {
            if (handler) *handler = &iter->handler;
            return 0;
        }
    }
    return HTTP_STATUS_METHOD_NOT_ALLOWED;
}

int HttpService::GetRoute(HttpRequest* req, http_handler** handler) {
    if (!req) {
        return HTTP_STATUS_NOT_FOUND;
    }
    return GetRoute(req->path.c_str(), req->method, handler, req->query_params);
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

void HttpService::AddTrustProxy(const char* host) {
    trustProxies.emplace_back(host);
}

void HttpService::AddNoProxy(const char* host) {
    noProxies.emplace_back(host);
}

bool HttpService::IsTrustProxy(const char* host) {
    if (!host || *host == '\0') return false;
    bool trust = true;
    if (trustProxies.size() != 0) {
        trust = false;
        for (const auto& trust_proxy : trustProxies) {
            if (hv_wildcard_match(host, trust_proxy.c_str())) {
                trust = true;
                break;
            }
        }
    }
    if (noProxies.size() != 0) {
        for (const auto& no_proxy : noProxies) {
            if (hv_wildcard_match(host, no_proxy.c_str())) {
                trust = false;
                break;
            }
        }
    }
    return trust;
}

void HttpService::AllowCORS() {
    Use(HttpMiddleware::CORS);
}

}
