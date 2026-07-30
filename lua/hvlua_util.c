#include "hvlua_util.h"

#include <lua.h>

int hvlua_parse_reconnect(lua_State* L, int table_index, reconn_setting_t* out) {
    if (!lua_istable(L, table_index)) return 0;
    lua_getfield(L, table_index, "reconnect");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return 0;
    }
    reconn_setting_init(out);
    lua_getfield(L, -1, "min_delay");
    if (lua_isinteger(L, -1)) out->min_delay = (uint32_t)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, -1, "max_delay");
    if (lua_isinteger(L, -1)) out->max_delay = (uint32_t)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, -1, "delay_policy");
    if (lua_isinteger(L, -1)) out->delay_policy = (uint32_t)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, -1, "max_retry");
    if (lua_isinteger(L, -1)) out->max_retry_cnt = (uint32_t)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_pop(L, 1);   // pop the reconnect sub-table
    return 1;
}
