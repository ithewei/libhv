#ifndef HV_HTTP_LUA_HANDLER_H_
#define HV_HTTP_LUA_HANDLER_H_

#include <memory>
#include <string>

#include "hexport.h"
#include "HttpService.h"

namespace hv {

struct HV_EXPORT HttpLuaHandlerOptions {
    bool reload_on_change;

    HttpLuaHandlerOptions() {
        reload_on_change = true;
    }
};

// HttpLuaHandler runs a Lua script to handle an HTTP request.
//
// Execution model (Stage B): the handler runs on the server IO thread and uses
// that thread's per-loop lua_State (EventLoop::luaState()). Each request runs
// the script's handler function inside a fresh coroutine, so the script may use
// synchronous-style async APIs (hloop.sleep, hv.dns.resolve, ...) that yield to
// the loop and resume on the same thread. If the script yields, the HTTP
// response is completed asynchronously via the HttpResponseWriter.
//
// The script defines either a per-method function (get/post/put/delete/...) or
// a generic handle(ctx); the per-method function takes precedence.
//
// The handler object itself is cheap and copyable; the compiled script lives in
// the per-loop lua_State and is (re)loaded on demand, tracking file mtime for
// hot reload.
class HV_EXPORT HttpLuaHandler {
public:
    HttpLuaHandler(const char* filepath, const HttpLuaHandlerOptions& options = HttpLuaHandlerOptions());

    int operator()(const HttpContextPtr& ctx);

    const std::string& filepath() const {
        return filepath_;
    }

private:
    std::string           filepath_;
    HttpLuaHandlerOptions options_;
};

typedef std::shared_ptr<HttpLuaHandler> HttpLuaHandlerPtr;

} // namespace hv

#endif // HV_HTTP_LUA_HANDLER_H_
