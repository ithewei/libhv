#include "HttpService.h"

void HttpService::AddApi(const char* path, http_method method, http_api_handler handler) {
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
            iter->handler = handler;
            return;
        }
    }
    // add
    method_handlers->push_back(http_method_handler(method, handler));
}

int HttpService::GetApi(const char* url, http_method method, http_api_handler* handler) {
    // {base_url}/path?query
    const char* s = url;
    const char* c = base_url.c_str();
    while (*s != '\0' && *c != '\0' && *s == *c) {++s;++c;}
    if (*c != '\0') {
        return HTTP_STATUS_NOT_FOUND;
    }
    const char* e = s;
    while (*e != '\0' && *e != '?') ++e;

    std::string path = std::string(s, e);
    auto iter = api_handlers.find(path);
    if (iter == api_handlers.end()) {
        *handler = NULL;
        return HTTP_STATUS_NOT_FOUND;
    }
    auto method_handlers = iter->second;
    for (auto iter = method_handlers->begin(); iter != method_handlers->end(); ++iter) {
        if (iter->method == method) {
            *handler = iter->handler;
            return 0;
        }
    }
    *handler = NULL;
    return HTTP_STATUS_METHOD_NOT_ALLOWED;
}

