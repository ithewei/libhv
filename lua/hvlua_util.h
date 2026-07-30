#ifndef HV_LUA_UTIL_H_
#define HV_LUA_UTIL_H_

// Small shared helpers for the lua bindings. Pure C (C-includable) so both the
// C bindings (hvlua_event.c) and the C++ bindings (hvlua_ws.cpp / hvlua_mqtt.cpp)
// can use them. Keep C++-only helpers (json <-> lua) out of here — those live in
// hvlua_json.h.

#include "hloop.h"   // reconn_setting_t

#ifndef lua_h
typedef struct lua_State lua_State;
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Parse an optional reconnect config sub-table from the Lua table at
// `table_index`:
//   reconnect = { min_delay=, max_delay=, delay_policy=, max_retry= }
// On success fills *out (starting from reconn_setting_init defaults, only the
// provided fields overridden) and returns 1. Returns 0 if there is no
// `reconnect` sub-table (out is left untouched). Used by hv.ws / hv.mqtt so the
// reconnect parsing lives in one place.
int hvlua_parse_reconnect(lua_State* L, int table_index, reconn_setting_t* out);

#ifdef __cplusplus
}
#endif

#endif // HV_LUA_UTIL_H_
