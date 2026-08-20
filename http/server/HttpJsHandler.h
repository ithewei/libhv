#ifndef HV_HTTP_JS_HANDLER_H_
#define HV_HTTP_JS_HANDLER_H_

#include <stddef.h>

#include <memory>
#include <string>

#include "hexport.h"
#include "HttpService.h"

namespace hv {

struct HV_EXPORT HttpJsHandlerOptions {
    bool reload_on_change;
    int timeout_ms;      // request wall-clock timeout; 0 disables
    size_t memory_limit; // QuickJS per-loop runtime memory limit; 0 disables
    size_t stack_size;   // QuickJS max stack size; 0 disables

    HttpJsHandlerOptions()
        : reload_on_change(true)
        , timeout_ms(30000)
        , memory_limit(64 * 1024 * 1024)
        , stack_size(1024 * 1024) {}
};

// HttpJsHandler runs a QuickJS script to handle an HTTP request.
//
// One QuickJS runtime is cached on each hloop_t, and each request gets its own
// JSContext for request globals and lifecycle. Scripts can use async functions
// and await hv.sleep() without blocking the server IO loop. The public route
// surface mirrors HttpLuaHandler: a per-method function (get/post/...) takes
// precedence over handle(ctx).
class HV_EXPORT HttpJsHandler {
public:
    HttpJsHandler(const char* filepath, const HttpJsHandlerOptions& options = HttpJsHandlerOptions());

    int operator()(const HttpContextPtr& ctx);

    const std::string& filepath() const { return filepath_; }

private:
    struct State;

    bool loadScript(std::string* code, std::string* err);

    std::string filepath_;
    HttpJsHandlerOptions options_;
    std::shared_ptr<State> state_;
};

typedef std::shared_ptr<HttpJsHandler> HttpJsHandlerPtr;

} // namespace hv

#endif // HV_HTTP_JS_HANDLER_H_
