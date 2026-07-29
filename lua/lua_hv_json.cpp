extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include "hv_lua.h"

#include <string>

#include "hstring.h"
#include "json.hpp"

using nlohmann::json;

// This module stays C++ (nlohmann::json). All helpers are file-static; only
// hvlua_open_json is exported, with C linkage so the C core (hv_lua.c) can call
// it.

// ---- lua <-> json conversion (shared style with HttpLuaHandler) ----

static json lua_to_json(lua_State* L, int index);

static json lua_table_to_json(lua_State* L, int index) {
    index = lua_absindex(L, index);
    bool is_array = true;
    lua_Integer max_index = 0;
    size_t count = 0;

    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        ++count;
        if (lua_type(L, -2) == LUA_TNUMBER && lua_isinteger(L, -2)) {
            lua_Integer k = lua_tointeger(L, -2);
            if (k <= 0) is_array = false;
            else if (k > max_index) max_index = k;
        } else {
            is_array = false;
        }
        lua_pop(L, 1);
    }

    if (is_array && (lua_Integer)count == max_index) {
        json j = json::array();
        for (lua_Integer i = 1; i <= max_index; ++i) {
            lua_geti(L, index, i);
            j.push_back(lua_to_json(L, -1));
            lua_pop(L, 1);
        }
        return j;
    }

    json j = json::object();
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
        if (!key.empty()) j[key] = lua_to_json(L, -1);
        lua_pop(L, 1);
    }
    return j;
}

static json lua_to_json(lua_State* L, int index) {
    switch (lua_type(L, index)) {
    case LUA_TNIL:     return nullptr;
    case LUA_TBOOLEAN: return lua_toboolean(L, index) != 0;
    case LUA_TNUMBER:
        if (lua_isinteger(L, index)) return (int64_t)lua_tointeger(L, index);
        return lua_tonumber(L, index);
    case LUA_TSTRING: {
        size_t len = 0;
        const char* s = lua_tolstring(L, index, &len);
        return std::string(s, len);
    }
    case LUA_TTABLE:   return lua_table_to_json(L, index);
    default:           return nullptr;
    }
}

static void json_to_lua(lua_State* L, const json& j) {
    switch (j.type()) {
    case json::value_t::null:
        lua_pushnil(L);
        break;
    case json::value_t::boolean:
        lua_pushboolean(L, j.get<bool>());
        break;
    case json::value_t::number_integer:
        lua_pushinteger(L, (lua_Integer)j.get<int64_t>());
        break;
    case json::value_t::number_unsigned:
        lua_pushinteger(L, (lua_Integer)j.get<uint64_t>());
        break;
    case json::value_t::number_float:
        lua_pushnumber(L, j.get<double>());
        break;
    case json::value_t::string: {
        const std::string& s = j.get_ref<const std::string&>();
        lua_pushlstring(L, s.data(), s.size());
        break;
    }
    case json::value_t::array: {
        lua_createtable(L, (int)j.size(), 0);
        int i = 1;
        for (const auto& item : j) {
            json_to_lua(L, item);
            lua_seti(L, -2, i++);
        }
        break;
    }
    case json::value_t::object: {
        lua_createtable(L, 0, (int)j.size());
        for (auto it = j.begin(); it != j.end(); ++it) {
            lua_pushlstring(L, it.key().data(), it.key().size());
            json_to_lua(L, it.value());
            lua_settable(L, -3);
        }
        break;
    }
    default:
        lua_pushnil(L);
        break;
    }
}

// hv.json.encode(value) -> string
static int l_hv_json_encode(lua_State* L) {
    json j = lua_to_json(L, 1);
    std::string s = j.dump();
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}

// hv.json.decode(string) -> value | nil,err
static int l_hv_json_decode(lua_State* L) {
    size_t len = 0;
    const char* s = luaL_checklstring(L, 1, &len);
    json j = json::parse(s, s + len, nullptr, false);
    if (j.is_discarded()) {
        lua_pushnil(L);
        lua_pushstring(L, "json parse error");
        return 2;
    }
    json_to_lua(L, j);
    return 1;
}

static const luaL_Reg hv_json_funcs[] = {
    { "encode", l_hv_json_encode },
    { "decode", l_hv_json_decode },
    { NULL, NULL }
};

// Add the hv.json subtable to the (already created) global "hv" table.
extern "C" void hvlua_open_json(lua_State* L) {
    lua_getglobal(L, "hv");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
    }
    luaL_newlib(L, hv_json_funcs);
    lua_setfield(L, -2, "json");
    lua_setglobal(L, "hv");
}
