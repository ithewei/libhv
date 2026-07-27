#ifndef HV_HTTP_LUA_HANDLER_H_
#define HV_HTTP_LUA_HANDLER_H_

#include <memory>
#include <mutex>
#include <string>
#include <time.h>

#include "hexport.h"
#include "HttpService.h"

struct lua_State;

namespace hv {

struct HV_EXPORT LuaHandlerOptions {
    bool reload_on_change;

    LuaHandlerOptions() {
        reload_on_change = true;
    }
};

class HV_EXPORT LuaHandler {
public:
    LuaHandler(const char* filepath, const LuaHandlerOptions& options = LuaHandlerOptions());
    LuaHandler(const LuaHandler& rhs);
    LuaHandler& operator=(const LuaHandler& rhs);
    ~LuaHandler();

    int operator()(const HttpContextPtr& ctx);

    const std::string& filepath() const {
        return filepath_;
    }

    std::string lastError() const;

private:
    bool reloadIfNeeded();
    bool loadLocked(time_t mtime);
    void closeLocked();
    int callLocked(const HttpContextPtr& ctx);
    void setErrorLocked(const std::string& error);

private:
    std::string       filepath_;
    LuaHandlerOptions options_;
    lua_State*        L_;
    time_t            mtime_;
    std::string       last_error_;
    mutable std::mutex mutex_;
};

typedef std::shared_ptr<LuaHandler> LuaHandlerPtr;

} // namespace hv

#endif // HV_HTTP_LUA_HANDLER_H_
