#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "hvlua.h"

#include <string.h>

#include "hlog.h"
#include "hversion.h"

// hv.version() -> libhv version string, e.g. "1.3.4"
static int l_hv_version(lua_State* L) {
    lua_pushstring(L, HV_VERSION_STRING);
    return 1;
}

// hv.log*(...) : join args with tabs and log at the given level.
// Uses a bounded stack buffer (log lines are short); overflow is truncated.
static int hvlua_log_at(lua_State* L, int level) {
    char line[1024];
    size_t off = 0;
    int n = lua_gettop(L);
    int i;
    line[0] = '\0';
    for (i = 1; i <= n; ++i) {
        size_t len = 0;
        const char* s = luaL_tolstring(L, i, &len);  // pushes a string repr
        if (i > 1 && off < sizeof(line) - 1) {
            line[off++] = '\t';
        }
        if (off < sizeof(line) - 1) {
            size_t space = sizeof(line) - 1 - off;
            size_t cpy = len < space ? len : space;
            memcpy(line + off, s, cpy);
            off += cpy;
        }
        lua_pop(L, 1);  // pop the string pushed by luaL_tolstring
    }
    line[off] = '\0';
    logger_print(hlog, level, "[lua] %s", line);
    return 0;
}

// hv.logd/logi/logw/loge : debug/info/warn/error. hv.log is an alias of logi.
static int l_hv_logd(lua_State* L) { return hvlua_log_at(L, LOG_LEVEL_DEBUG); }
static int l_hv_logi(lua_State* L) { return hvlua_log_at(L, LOG_LEVEL_INFO);  }
static int l_hv_logw(lua_State* L) { return hvlua_log_at(L, LOG_LEVEL_WARN);  }
static int l_hv_loge(lua_State* L) { return hvlua_log_at(L, LOG_LEVEL_ERROR); }

static const luaL_Reg hv_base_funcs[] = {
    { "version", l_hv_version },
    { "log",     l_hv_logi    },  // alias of logi
    { "logd",    l_hv_logd    },
    { "logi",    l_hv_logi    },
    { "logw",    l_hv_logw    },
    { "loge",    l_hv_loge    },
    { NULL, NULL }
};

// Create/extend the global "hv" table with the base functions.
void hvlua_open_base(lua_State* L) {
    lua_getglobal(L, "hv");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
    }
    luaL_setfuncs(L, hv_base_funcs, 0);
    lua_setglobal(L, "hv");
}
