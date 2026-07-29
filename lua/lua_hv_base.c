#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "hv_lua.h"

#include <string.h>
#include <time.h>

#include "hlog.h"

// hv.log(...) : join args with tabs and log at INFO level.
// Uses a bounded stack buffer (log lines are short); overflow is truncated.
static int l_hv_log(lua_State* L) {
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
    hlogi("[lua] %s", line);
    return 0;
}

// hv.now() -> unix seconds
static int l_hv_now(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)time(NULL));
    return 1;
}

static const luaL_Reg hv_base_funcs[] = {
    { "log", l_hv_log },
    { "now", l_hv_now },
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
