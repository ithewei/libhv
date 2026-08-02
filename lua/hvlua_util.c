#include "hvlua_util.h"

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
    if (out->min_delay == 0) out->min_delay = 1;
    if (out->max_delay < out->min_delay) out->max_delay = out->min_delay;
    if (out->delay_policy > 1 && out->delay_policy > UINT32_MAX / out->min_delay) {
        out->delay_policy = DEFAULT_RECONNECT_DELAY_POLICY;
    }
    lua_pop(L, 1);   // pop the reconnect sub-table
    return 1;
}

int hvlua_new_class(lua_State* L, const char* mt_name, lua_CFunction gc, const luaL_Reg* methods) {
    if (!luaL_newmetatable(L, mt_name)) return 0;   // already registered; leaves mt on top
    lua_pushcfunction(L, gc);
    lua_setfield(L, -2, "__gc");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");   // methods live on the metatable itself
    if (methods) luaL_setfuncs(L, methods, 0);
    return 1;
}
