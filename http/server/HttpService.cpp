#include "HttpService.h"
#include "HttpMiddleware.h"

#include "hbase.h" // import hv_strendswith

namespace hv {

static bool has_wildcard(const char* path) {
    return path && strchr(path, '*');
}

static void split_path(const std::string& path, std::vector<std::string>& segments) {
    if (path.empty()) return;
    size_t i = 0;
    while (i < path.size() && path[i] == '/') ++i;
    for (;;) {
        size_t j = i;
        while (j < path.size() && path[j] != '/') ++j;
        if (i < path.size() || (j == path.size() && !segments.empty())) {
            segments.emplace_back(path.substr(i, j - i));
        }
        if (j == path.size()) break;
        i = j + 1;
        if (i == path.size()) {
            segments.emplace_back("");
            break;
        }
    }
}

static std::string parse_param_name(const std::string& segment) {
    if (segment.empty()) return "";
    if (segment[0] == ':') {
        return segment.substr(1);
    }
    if (segment[0] == '{') {
        if (segment.size() >= 2 && segment.back() == '}') {
            return segment.substr(1, segment.size() - 2);
        }
        return segment.substr(1);
    }
    return "";
}

static std::shared_ptr<route_trie_node> match_route_trie(
        const std::shared_ptr<route_trie_node>& node,
        const std::vector<std::string>& segments,
        size_t index,
        std::map<std::string, std::string>& params) {
    if (!node) return NULL;
    if (index == segments.size()) {
        return node->method_handlers ? node : NULL;
    }

    auto iter = node->children.find(segments[index]);
    if (iter != node->children.end()) {
        auto found = match_route_trie(iter->second, segments, index + 1, params);
        if (found) return found;
    }

    if (node->param_child) {
        params[node->param_child->param_name] = segments[index];
        auto found = match_route_trie(node->param_child, segments, index + 1, params);
        if (found) return found;
        params.erase(node->param_child->param_name);
    }

    return NULL;
}

static int get_method_handler(std::shared_ptr<http_method_handlers> method_handlers, http_method method, http_handler** handler) {
    for (auto iter = method_handlers->begin(); iter != method_handlers->end(); ++iter) {
        if (iter->method == method) {
            if (handler) *handler = &iter->handler;
            return 0;
        }
    }
    if (handler) *handler = NULL;
    return HTTP_STATUS_METHOD_NOT_ALLOWED;
}

static bool match_wildcard_route(const std::string& pattern, const std::string& path) {
    const char* kp = pattern.c_str();
    const char* vp = path.c_str();
    while (*kp && *vp) {
        if (kp[0] == '*') {
            return hv_strendswith(vp, kp + 1);
        }
        if (*kp != *vp) {
            return false;
        }
        ++kp;
        ++vp;
    }
    return *kp == '\0' && *vp == '\0';
}

void HttpService::AddRoute(const char* path, http_method method, const http_handler& handler) {
    std::shared_ptr<http_method_handlers> method_handlers = NULL;
    bool is_new_path = false;
    auto iter = pathHandlers.find(path);
    if (iter == pathHandlers.end()) {
        // add path
        method_handlers = std::make_shared<http_method_handlers>();
        pathHandlers[path] = method_handlers;
        is_new_path = true;
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

    if (!is_new_path) return;

    std::string str_path(path);
    if (has_wildcard(path)) {
        wildcardHandlers.emplace_back(str_path, method_handlers);
        return;
    }

    std::vector<std::string> segments;
    split_path(str_path, segments);
    auto node = routeTrie;
    for (size_t i = 0; i < segments.size(); ++i) {
        const auto& segment = segments[i];
        std::string param_name = parse_param_name(segment);
        if (!param_name.empty()) {
            if (!node->param_child) {
                node->param_child = std::make_shared<route_trie_node>();
            }
            node = node->param_child;
            node->param_name = param_name;
        } else {
            auto child_iter = node->children.find(segment);
            if (child_iter == node->children.end()) {
                node->children[segment] = std::make_shared<route_trie_node>();
            }
            node = node->children[segment];
        }
    }
    node->method_handlers = method_handlers;
}

int HttpService::GetRoute(const char* url, http_method method, http_handler** handler) {
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
    auto iter = pathHandlers.find(path);
    if (iter == pathHandlers.end()) {
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

int HttpService::GetRoute(HttpRequest* req, http_handler** handler) {
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
    std::vector<std::string> segments;
    split_path(path, segments);
    std::map<std::string, std::string> params;
    auto route_node = match_route_trie(routeTrie, segments, 0, params);
    if (route_node) {
        int ret = get_method_handler(route_node->method_handlers, req->method, handler);
        if (ret == 0) {
            for (auto& param : params) {
                req->query_params[param.first] = param.second;
            }
        }
        return ret;
    }

    bool method_not_allowed = false;
    for (const auto& route : wildcardHandlers) {
        if (!match_wildcard_route(route.first, path)) continue;
        int ret = get_method_handler(route.second, req->method, handler);
        if (ret == 0) return 0;
        method_not_allowed = true;
    }

    if (method_not_allowed) {
        if (handler) *handler = NULL;
        return HTTP_STATUS_METHOD_NOT_ALLOWED;
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
