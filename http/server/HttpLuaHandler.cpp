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

#include "EventLoop.h"
#include "hv_lua.h"

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
    holder->~LuaHttpContext();
    return 0;
}

static void lua_push_ctx(lua_State* L, const HttpContextPtr& ctx) {
    void* storage = lua_newuserdata(L, sizeof(LuaHttpContext));
    new (storage) LuaHttpContext();
    ((LuaHttpContext*)storage)->ctx = ctx;
    luaL_getmetatable(L, LUA_CTX_META);
    lua_setmetatable(L, -2);
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

static time_t file_mtime(const std::string& filepath) {
    struct stat st;
    if (stat(filepath.c_str(), &st) != 0) {
        return 0;
    }
    return st.st_mtime;
}

// Per-loop script cache: a registry table "hv.lua_http_scripts" mapping
// filepath -> { env = <table>, mtime = <int> }. Each script is loaded into its
// own environment table (its globals), so multiple scripts on one lua_State do
// not clobber each other's handle/get/post functions.
static const char* SCRIPTS_REG = "hv.lua_http_scripts";

// Push (loading/reloading as needed) the script's env table onto L.
// Returns true with the env table on the stack top, or false with an error
// message on top.
static bool push_script_env(lua_State* L, const std::string& filepath,
                            const HttpLuaHandlerOptions& options) {
    time_t mtime = file_mtime(filepath);
    if (mtime == 0) {
        lua_pushstring(L, strerror(errno));
        return false;
    }

    // registry[SCRIPTS_REG] (create if missing)
    lua_getfield(L, LUA_REGISTRYINDEX, SCRIPTS_REG);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, SCRIPTS_REG);
    }
    // scripts[filepath]
    lua_getfield(L, -1, filepath.c_str());
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "mtime");
        time_t cached = (time_t)lua_tointeger(L, -1);
        lua_pop(L, 1);
        if (!options.reload_on_change || cached == mtime) {
            lua_getfield(L, -1, "env");     // -> scripts, entry, env
            lua_remove(L, -2);              // -> scripts, env
            lua_remove(L, -2);              // -> env
            return true;
        }
    }
    lua_pop(L, 1);  // pop entry (nil or stale); stack: scripts

    // Load the chunk.
    if (luaL_loadfile(L, filepath.c_str()) != LUA_OK) {
        std::string err = lua_tostring(L, -1) ? lua_tostring(L, -1) : "load failed";
        lua_pop(L, 2);  // chunk err + scripts
        lua_pushstring(L, err.c_str());
        return false;
    }
    // New environment table with an __index to _G so scripts can use globals
    // (hloop, hv, print, ...) while their own defs stay isolated.
    lua_newtable(L);                        // env
    lua_newtable(L);                        // metatable
    lua_pushglobaltable(L);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);                // setmetatable(env, {__index=_G})
    // set the chunk's _ENV upvalue to env (Lua 5.2+: first upvalue)
    lua_pushvalue(L, -1);                   // env copy
#if LUA_VERSION_NUM >= 502
    const char* upname = lua_setupvalue(L, -3, 1); // chunk's _ENV = env
    if (upname == NULL) lua_pop(L, 1);
#else
    lua_setfenv(L, -3);
#endif
    // stack: scripts, chunk, env
    lua_pushvalue(L, -2);                   // chunk
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {  // run chunk to populate env
        std::string err = lua_tostring(L, -1) ? lua_tostring(L, -1) : "run failed";
        lua_pop(L, 3);  // err, chunk, scripts
        lua_pushstring(L, err.c_str());
        return false;
    }
    // stack: scripts, chunk, env
    lua_remove(L, -2);                      // scripts, env

    // Cache: scripts[filepath] = { env = env, mtime = mtime }
    lua_newtable(L);                        // entry
    lua_pushvalue(L, -2);                   // env
    lua_setfield(L, -2, "env");
    lua_pushinteger(L, (lua_Integer)mtime);
    lua_setfield(L, -2, "mtime");
    lua_setfield(L, -3, filepath.c_str());  // scripts[filepath] = entry
    // stack: scripts, env
    lua_remove(L, -2);                      // env
    return true;
}

// Resolve the handler function for this request from the script env: prefer a
// per-method function (get/post/...), else handle. Pushes the function on L, or
// pushes nil if none found. Consumes nothing (env stays where it was).
static bool push_handler_fn(lua_State* L, int env_index, http_method method) {
    std::string name = http_method_str(method);
    tolower(name);
    lua_getfield(L, env_index, name.c_str());
    if (lua_isfunction(L, -1)) return true;
    lua_pop(L, 1);
    lua_getfield(L, env_index, "handle");
    if (lua_isfunction(L, -1)) return true;
    lua_pop(L, 1);
    return false;
}

// Build the HTTP response from the coroutine's return value (top of `co`).
static void apply_result(lua_State* co, const HttpContextPtr& ctx) {
    if (lua_isinteger(co, -1)) {
        int status = (int)lua_tointeger(co, -1);
        if (ctx->response->status_code == HTTP_STATUS_OK) {
            ctx->response->status_code = (http_status)status;
        }
    } else if (lua_isstring(co, -1)) {
        size_t len = 0;
        const char* s = lua_tolstring(co, -1, &len);
        ctx->response->String(std::string(s, len));
    } else if (lua_istable(co, -1)) {
        Json j = lua_to_json(co, -1);
        ctx->response->Json(j);
    }
}

// Task completion state shared between operator() and on_task_done.
struct LuaHttpTask {
    HttpContextPtr ctx;
    bool async;      // set true once operator() knows the coroutine yielded
};

static void on_task_done(void* ud, bool ok, lua_State* co) {
    LuaHttpTask* task = (LuaHttpTask*)ud;
    HttpContextPtr ctx = task->ctx;
    bool async = task->async;
    delete task;

    if (!ok) {
        const char* msg = co ? lua_tostring(co, -1) : NULL;
        std::string err = msg ? msg : "lua handler error";
        hloge("[lua] http handler error: %s", err.c_str());
        ctx->response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
        if (ctx->response->body.empty()) ctx->response->String(err);
    } else if (co) {
        apply_result(co, ctx);
    }

    // For the async (yielded) path, the normal HttpHandler flow already
    // returned NEXT, so we must flush the response ourselves now.
    if (async) {
        ctx->send();
    }
}

} // namespace

HttpLuaHandler::HttpLuaHandler(const char* filepath, const HttpLuaHandlerOptions& options)
    : filepath_(filepath ? filepath : "")
    , options_(options) {
}

int HttpLuaHandler::operator()(const HttpContextPtr& ctx) {
    if (!ctx || !ctx->response || !ctx->request) {
        return HTTP_STATUS_INTERNAL_SERVER_ERROR;
    }

    EventLoop* loop = currentThreadEventLoop;
    if (loop == NULL) {
        ctx->response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
        ctx->response->String("lua handler: no event loop on this thread");
        return HTTP_STATUS_INTERNAL_SERVER_ERROR;
    }
    lua_State* L = hvlua_state(loop->loop());
    if (L == NULL) {
        ctx->response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
        ctx->response->String("lua handler: failed to create lua state");
        return HTTP_STATUS_INTERNAL_SERVER_ERROR;
    }

    // Ensure the HttpContext metatable is registered on this per-loop state.
    register_ctx(L);

    // Load/reload the script; on failure return 500 with the error.
    if (!push_script_env(L, filepath_, options_)) {
        std::string err = lua_tostring(L, -1) ? lua_tostring(L, -1) : "load error";
        lua_pop(L, 1);
        hloge("load lua script %s failed: %s", filepath_.c_str(), err.c_str());
        ctx->response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
        ctx->response->String(err);
        return HTTP_STATUS_INTERNAL_SERVER_ERROR;
    }
    // stack: env
    if (!push_handler_fn(L, -1, ctx->request->method)) {
        lua_pop(L, 1);  // env
        hloge("lua script %s: no handler (get/post/.../handle)", filepath_.c_str());
        ctx->response->status_code = HTTP_STATUS_NOT_IMPLEMENTED;
        ctx->response->String("no lua handler function");
        return HTTP_STATUS_NOT_IMPLEMENTED;
    }
    // stack: env, fn
    lua_remove(L, -2);  // stack: fn
    lua_push_ctx(L, ctx);  // stack: fn, ctx  (the handler's single argument)

    LuaHttpTask* task = new LuaHttpTask();
    task->ctx = ctx;
    task->async = false;

    // Run fn(ctx) in a coroutine. If it finishes synchronously, on_task_done
    // runs now (async=false) and builds the response; we return the status so
    // the normal HttpHandler flow sends it. If it yields, we return NEXT and
    // the response is flushed later in on_task_done (async=true).
    int finished = hvlua_start_task(L, 1, on_task_done, task);
    if (finished) {
        return ctx->response->status_code;
    }
    task->async = true;
    return HTTP_STATUS_NEXT;  // 0: response completed asynchronously
}

} // namespace hv

#endif // WITH_LUA

