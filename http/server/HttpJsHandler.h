#ifndef HV_HTTP_JS_HANDLER_H_
#define HV_HTTP_JS_HANDLER_H_

#include <memory>
#include <string>

#include "hexport.h"
#include "HttpService.h"

namespace hv {

struct HV_EXPORT HttpJsHandlerOptions {
    bool reload_on_change;

    HttpJsHandlerOptions() { reload_on_change = true; }
};

// HttpJsHandler runs a QuickJS script to handle an HTTP request.
//
// The first implementation uses one QuickJS runtime/context per request. This
// keeps request lifetime, Promise continuations and loop-thread affinity simple;
// scripts can use async functions and await hv.sleep() without blocking the
// server IO loop. The public route surface mirrors HttpLuaHandler: a per-method
// function (get/post/...) takes precedence over handle(ctx).
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
