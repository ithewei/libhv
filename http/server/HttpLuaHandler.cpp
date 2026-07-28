#include "HttpLuaHandler.h"

#ifdef WITH_LUA

#include <errno.h>
#include <string.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include "hfile.h"
#include "hlog.h"
#include "hpath.h"
#include "hstring.h"
#include "htime.h"

namespace hv {

namespace {

static const char* LUA_CTX_META = "hv.HttpContext";

struct LuaHttpContext {
    HttpContextPtr ctx;
};

static LuaHttpContext* lua_check_ctx(lua_State* L) {
    return (LuaHttpContext*)luaL_checkudata(L, 1, LUA_CTX_META);
}

static std::string lua_opt_string_arg(lua_State* L, int index, const char* defvalue = "") {
    if (lua_isnoneornil(L, index)) {
        return defvalue;
    }
    size_t len = 0;
    const char* s = luaL_checklstring(L, index, &len);
    return std::string(s, len);
}

static int lua_ctx_method(lua_State* L) {
    LuaHttpContext* holder = lua_check_ctx(L);
    lua_pushstring(L, http_method_str(holder->ctx->request->method));
    return 1;
}

static int lua_ctx_path(lua_State* L) {
    LuaHttpContext* holder = lua_check_ctx(L);
    lua_pushlstring(L, holder->ctx->request->path.data(), holder->ctx->request->path.size());
    return 1;
}

static int lua_ctx_param(lua_State* L) {
    LuaHttpContext* holder = lua_check_ctx(L);
    std::string key = lua_opt_string_arg(L, 2);
    std::string defvalue = lua_opt_string_arg(L, 3);
    lua_pushstring(L, holder->ctx->request->GetParam(key.c_str(), defvalue).c_str());
    return 1;
}

static int lua_ctx_query(lua_State* L) {
    return lua_ctx_param(L);
}

static int lua_ctx_header(lua_State* L) {
    LuaHttpContext* holder = lua_check_ctx(L);
    std::string key = lua_opt_string_arg(L, 2);
    std::string defvalue = lua_opt_string_arg(L, 3);
    lua_pushstring(L, holder->ctx->request->GetHeader(key.c_str(), defvalue).c_str());
    return 1;
}

static int lua_ctx_body(lua_State* L) {
    LuaHttpContext* holder = lua_check_ctx(L);
    const std::string& body = holder->ctx->request->body;
    lua_pushlstring(L, body.data(), body.size());
    return 1;
}

static int lua_ctx_status(lua_State* L) {
    LuaHttpContext* holder = lua_check_ctx(L);
    int status = (int)luaL_checkinteger(L, 2);
    holder->ctx->response->status_code = (http_status)status;
    lua_pushinteger(L, status);
    return 1;
}

static int lua_ctx_set_header(lua_State* L) {
    LuaHttpContext* holder = lua_check_ctx(L);
    std::string key = lua_opt_string_arg(L, 2);
    std::string value = lua_opt_string_arg(L, 3);
    holder->ctx->response->SetHeader(key.c_str(), value);
    if (stricmp(key.c_str(), "Content-Type") == 0) {
        holder->ctx->response->SetContentType(value.c_str());
    }
    return 0;
}

static int lua_ctx_text(lua_State* L) {
    LuaHttpContext* holder = lua_check_ctx(L);
    std::string text = lua_opt_string_arg(L, 2);
    holder->ctx->response->String(text);
    lua_pushinteger(L, holder->ctx->response->status_code);
    return 1;
}

static Json lua_to_json(lua_State* L, int index);

static Json lua_table_to_json(lua_State* L, int index) {
    index = lua_absindex(L, index);
    bool is_array = true;
    lua_Integer max_index = 0;
    size_t count = 0;

    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        ++count;
        if (lua_type(L, -2) == LUA_TNUMBER && lua_isinteger(L, -2)) {
            lua_Integer k = lua_tointeger(L, -2);
            if (k <= 0) {
                is_array = false;
            } else if (k > max_index) {
                max_index = k;
            }
        } else {
            is_array = false;
        }
        lua_pop(L, 1);
    }

    if (is_array && (lua_Integer)count == max_index) {
        Json j = Json::array();
        for (lua_Integer i = 1; i <= max_index; ++i) {
            lua_geti(L, index, i);
            j.push_back(lua_to_json(L, -1));
            lua_pop(L, 1);
        }
        return j;
    }

    Json j = Json::object();
    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        std::string key;
        if (lua_type(L, -2) == LUA_TSTRING) {
            size_t len = 0;
            const char* s = lua_tolstring(L, -2, &len);
            key.assign(s, len);
        } else if (lua_type(L, -2) == LUA_TNUMBER) {
            key = hv::to_string((int64_t)lua_tointeger(L, -2));
        }
        if (!key.empty()) {
            j[key] = lua_to_json(L, -1);
        }
        lua_pop(L, 1);
    }
    return j;
}

static Json lua_to_json(lua_State* L, int index) {
    switch (lua_type(L, index)) {
    case LUA_TNIL:
        return nullptr;
    case LUA_TBOOLEAN:
        return lua_toboolean(L, index) != 0;
    case LUA_TNUMBER:
        if (lua_isinteger(L, index)) {
            return (int64_t)lua_tointeger(L, index);
        }
        return lua_tonumber(L, index);
    case LUA_TSTRING: {
        size_t len = 0;
        const char* s = lua_tolstring(L, index, &len);
        return std::string(s, len);
    }
    case LUA_TTABLE:
        return lua_table_to_json(L, index);
    default:
        return nullptr;
    }
}

static int lua_ctx_json(lua_State* L) {
    LuaHttpContext* holder = lua_check_ctx(L);
    Json j = lua_to_json(L, 2);
    holder->ctx->response->Json(j);
    lua_pushinteger(L, holder->ctx->response->status_code);
    return 1;
}

static int lua_ctx_gc(lua_State* L) {
    LuaHttpContext* holder = lua_check_ctx(L);
    holder->ctx.~shared_ptr<hv::HttpContext>();
    return 0;
}

static void lua_push_ctx(lua_State* L, const HttpContextPtr& ctx) {
    void* storage = lua_newuserdata(L, sizeof(LuaHttpContext));
    new (storage) LuaHttpContext();
    ((LuaHttpContext*)storage)->ctx = ctx;
    luaL_getmetatable(L, LUA_CTX_META);
    lua_setmetatable(L, -2);
}

static int lua_hv_log(lua_State* L) {
    int n = lua_gettop(L);
    std::string line;
    for (int i = 1; i <= n; ++i) {
        size_t len = 0;
        const char* s = luaL_tolstring(L, i, &len);
        if (i > 1) line += "\t";
        line.append(s, len);
        lua_pop(L, 1);
    }
    hlogi("[lua] %s", line.c_str());
    return 0;
}

static int lua_hv_now(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)time(NULL));
    return 1;
}

static void register_ctx(lua_State* L) {
    if (luaL_newmetatable(L, LUA_CTX_META)) {
        lua_pushcfunction(L, lua_ctx_gc);
        lua_setfield(L, -2, "__gc");

        lua_newtable(L);
        lua_pushcfunction(L, lua_ctx_method);
        lua_setfield(L, -2, "method");
        lua_pushcfunction(L, lua_ctx_path);
        lua_setfield(L, -2, "path");
        lua_pushcfunction(L, lua_ctx_param);
        lua_setfield(L, -2, "param");
        lua_pushcfunction(L, lua_ctx_query);
        lua_setfield(L, -2, "query");
        lua_pushcfunction(L, lua_ctx_header);
        lua_setfield(L, -2, "header");
        lua_pushcfunction(L, lua_ctx_body);
        lua_setfield(L, -2, "body");
        lua_pushcfunction(L, lua_ctx_status);
        lua_setfield(L, -2, "status");
        lua_pushcfunction(L, lua_ctx_set_header);
        lua_setfield(L, -2, "set_header");
        lua_pushcfunction(L, lua_ctx_text);
        lua_setfield(L, -2, "text");
        lua_pushcfunction(L, lua_ctx_json);
        lua_setfield(L, -2, "json");
        lua_setfield(L, -2, "__index");
    }
    lua_pop(L, 1);
}

static void register_hv(lua_State* L) {
    lua_newtable(L);
    lua_pushcfunction(L, lua_hv_log);
    lua_setfield(L, -2, "log");
    lua_pushcfunction(L, lua_hv_now);
    lua_setfield(L, -2, "now");
    lua_setglobal(L, "hv");
}

static time_t file_mtime(const std::string& filepath) {
    struct stat st;
    if (stat(filepath.c_str(), &st) != 0) {
        return 0;
    }
    return st.st_mtime;
}

} // namespace

LuaHandler::LuaHandler(const char* filepath, const LuaHandlerOptions& options)
    : filepath_(filepath ? filepath : "")
    , options_(options)
    , L_(NULL)
    , mtime_(0) {
}

LuaHandler::LuaHandler(const LuaHandler& rhs)
    : filepath_(rhs.filepath_)
    , options_(rhs.options_)
    , L_(NULL)
    , mtime_(0) {
}

LuaHandler& LuaHandler::operator=(const LuaHandler& rhs) {
    if (this == &rhs) return *this;
    std::lock_guard<std::mutex> lock(mutex_);
    closeLocked();
    filepath_ = rhs.filepath_;
    options_ = rhs.options_;
    mtime_ = 0;
    last_error_.clear();
    return *this;
}

LuaHandler::~LuaHandler() {
    std::lock_guard<std::mutex> lock(mutex_);
    closeLocked();
}

std::string LuaHandler::lastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_error_;
}

void LuaHandler::setErrorLocked(const std::string& error) {
    last_error_ = error;
}

void LuaHandler::closeLocked() {
    if (L_) {
        lua_close(L_);
        L_ = NULL;
    }
}

bool LuaHandler::loadLocked(time_t mtime) {
    lua_State* L = luaL_newstate();
    if (L == NULL) {
        setErrorLocked("luaL_newstate failed");
        return false;
    }

    luaL_openlibs(L);
    register_ctx(L);
    register_hv(L);

    if (luaL_loadfile(L, filepath_.c_str()) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
        std::string error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "load script failed";
        lua_close(L);
        setErrorLocked(error);
        hloge("load lua script %s failed: %s", filepath_.c_str(), error.c_str());
        return false;
    }

    lua_getglobal(L, "handle");
    if (!lua_isfunction(L, -1)) {
        lua_close(L);
        setErrorLocked("global handle(ctx) is not a function");
        hloge("load lua script %s failed: handle(ctx) not found", filepath_.c_str());
        return false;
    }
    lua_pop(L, 1);

    closeLocked();
    L_ = L;
    mtime_ = mtime;
    last_error_.clear();
    return true;
}

bool LuaHandler::reloadIfNeeded() {
    std::lock_guard<std::mutex> lock(mutex_);
    time_t mtime = file_mtime(filepath_);
    if (mtime == 0) {
        setErrorLocked(strerror(errno));
        return L_ != NULL;
    }
    if (L_ != NULL && (!options_.reload_on_change || mtime == mtime_)) {
        return true;
    }
    return loadLocked(mtime) || L_ != NULL;
}

int LuaHandler::callLocked(const HttpContextPtr& ctx) {
    std::string handler_name = http_method_str(ctx->request->method);
    tolower(handler_name);
    lua_getglobal(L_, handler_name.c_str());
    if (!lua_isfunction(L_, -1)) {
        lua_pop(L_, 1);
        lua_getglobal(L_, "handle");
    }
    lua_push_ctx(L_, ctx);
    if (lua_pcall(L_, 1, 1, 0) != LUA_OK) {
        std::string error = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "call handle failed";
        lua_pop(L_, 1);
        setErrorLocked(error);
        hloge("call lua script %s failed: %s", filepath_.c_str(), error.c_str());
        ctx->response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
        ctx->response->String(error);
        return HTTP_STATUS_INTERNAL_SERVER_ERROR;
    }

    int status = ctx->response->status_code;
    if (lua_isinteger(L_, -1)) {
        status = (int)lua_tointeger(L_, -1);
        if (ctx->response->status_code == HTTP_STATUS_OK) {
            ctx->response->status_code = (http_status)status;
        }
    } else if (lua_isstring(L_, -1)) {
        size_t len = 0;
        const char* s = lua_tolstring(L_, -1, &len);
        ctx->response->String(std::string(s, len));
        status = ctx->response->status_code;
    } else if (lua_istable(L_, -1)) {
        Json j = lua_to_json(L_, -1);
        ctx->response->Json(j);
        status = ctx->response->status_code;
    }
    lua_pop(L_, 1);
    return status;
}

int LuaHandler::operator()(const HttpContextPtr& ctx) {
    if (!ctx || !ctx->response || !ctx->request) {
        return HTTP_STATUS_INTERNAL_SERVER_ERROR;
    }
    if (!reloadIfNeeded()) {
        std::lock_guard<std::mutex> lock(mutex_);
        ctx->response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
        ctx->response->String(last_error_);
        return HTTP_STATUS_INTERNAL_SERVER_ERROR;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    return callLocked(ctx);
}

} // namespace hv

#endif // WITH_LUA
