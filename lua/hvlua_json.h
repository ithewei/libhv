#ifndef HV_LUA_JSON_H_
#define HV_LUA_JSON_H_

// Shared lua <-> nlohmann::json conversion, used by both the hv.json binding
// (lua/hvlua_json.cpp) and the HTTP Lua handler (http/server/HttpLuaHandler.cpp)
// so the conversion (and its safety guards: recursion-depth cap for cyclic
// tables) lives in ONE place.
//
// C++ only (pulls in json.hpp). The pure-C lua files (hvlua.c / hvlua_event.c /
// hvlua_base.c) must not include this.

#include "json.hpp"   // nlohmann::json

struct lua_State;

namespace hv {

// Max lua -> json nesting depth. A self-referential table (t.self=t) or
// pathologically deep nesting would otherwise recurse until the C stack
// overflows (SIGSEGV); conversion stops descending past this depth.
#define HVLUA_JSON_MAX_DEPTH 64

// Convert the Lua value at stack `index` to json. `depth` is the current
// nesting level (callers pass 0); tables deeper than HVLUA_JSON_MAX_DEPTH are
// converted to null instead of recursing.
nlohmann::json hvlua_lua_to_json(lua_State* L, int index, int depth = 0);

// Push `j` onto the Lua stack as the corresponding Lua value.
void hvlua_json_to_lua(lua_State* L, const nlohmann::json& j);

} // namespace hv

#endif // HV_LUA_JSON_H_
