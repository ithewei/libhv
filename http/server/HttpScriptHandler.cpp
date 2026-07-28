#include "HttpScriptHandler.h"

#ifdef WITH_LUA

#include "hbase.h"
#include "hstring.h"
#include "HttpLuaHandler.h"

namespace hv {

struct HttpScriptHandler::State {
    HttpLuaHandlerPtr lua_handler;
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
    if (filepath_has_suffix(filepath_, "lua")) {
        if (!state_->lua_handler) {
            HttpLuaHandlerOptions lua_options;
            lua_options.reload_on_change = options_.reload_on_change;
            state_->lua_handler = std::make_shared<HttpLuaHandler>(filepath_.c_str(), lua_options);
        }
        return (*state_->lua_handler)(ctx);
    }

    if (ctx && ctx->response) {
        ctx->response->status_code = HTTP_STATUS_NOT_IMPLEMENTED;
        ctx->response->String("unsupported script type");
    }
    return HTTP_STATUS_NOT_IMPLEMENTED;
}

} // namespace hv

#endif // WITH_LUA
