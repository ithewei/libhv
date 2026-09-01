#include "HttpScriptHandler.h"

#if defined(WITH_LUA) || defined(WITH_JS)

#include "hbase.h"
#include "hstring.h"
#ifdef WITH_JS
#include "HttpJsHandler.h"
#endif
#ifdef WITH_LUA
#include "HttpLuaHandler.h"
#endif
#include <mutex>

namespace hv {

struct HttpScriptHandler::State {
#ifdef WITH_LUA
    std::once_flag    lua_once;
    HttpLuaHandlerPtr lua_handler;
#endif
#ifdef WITH_JS
    std::once_flag   js_once;
    HttpJsHandlerPtr js_handler;
#endif
};

namespace {

static bool filepath_has_suffix(const std::string& filepath, const char* suffix) {
    std::string ext = hv_suffixname(filepath.c_str());
    return stricmp(ext.c_str(), suffix) == 0;
}

} // namespace

HttpScriptHandler::HttpScriptHandler(const char* filepath, const HttpScriptHandlerOptions& options)
    : filepath_(filepath ? filepath : "")
    , options_(options)
    , state_(std::make_shared<State>()) {
}

HttpScriptHandler::HttpScriptHandler(const HttpScriptHandler& rhs)
    : filepath_(rhs.filepath_)
    , options_(rhs.options_)
    , state_(std::make_shared<State>()) {
}

HttpScriptHandler& HttpScriptHandler::operator=(const HttpScriptHandler& rhs) {
    if (this == &rhs) return *this;
    filepath_ = rhs.filepath_;
    options_ = rhs.options_;
    state_ = std::make_shared<State>();
    return *this;
}

int HttpScriptHandler::operator()(const HttpContextPtr& ctx) {
#ifdef WITH_LUA
    if (filepath_has_suffix(filepath_, "lua")) {
        std::call_once(state_->lua_once, [this]() {
            HttpLuaHandlerOptions lua_options;
            lua_options.reload_on_change = options_.reload_on_change;
            state_->lua_handler = std::make_shared<HttpLuaHandler>(filepath_.c_str(), lua_options);
        });
        return (*state_->lua_handler)(ctx);
    }
#endif

#ifdef WITH_JS
    if (filepath_has_suffix(filepath_, "js")) {
        std::call_once(state_->js_once, [this]() {
            HttpJsHandlerOptions js_options;
            js_options.reload_on_change = options_.reload_on_change;
            state_->js_handler = std::make_shared<HttpJsHandler>(filepath_.c_str(), js_options);
        });
        return (*state_->js_handler)(ctx);
    }
#endif

    if (ctx && ctx->response) {
        ctx->response->status_code = HTTP_STATUS_NOT_IMPLEMENTED;
        ctx->response->String("unsupported script type");
    }
    return HTTP_STATUS_NOT_IMPLEMENTED;
}

} // namespace hv

#endif // WITH_LUA || WITH_JS
