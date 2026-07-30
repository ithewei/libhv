/*
 * lua_mqtt_test — smoke test for the hv.mqtt Lua binding (hvlua_mqtt.cpp).
 *
 * No live MQTT broker is required: this verifies the module registers under the
 * hv.* table and that its connect() failure path returns (nil, err) on the
 * coroutine without crashing or hanging (connection refused to a closed port).
 * The single-loop model means the failure callback fires on this thread and
 * resumes the coroutine.
 */

#include <assert.h>
#include <stdio.h>
#include <string>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include "hloop.h"
#include "mqtt_client.h"
#include "EventLoop.h"
#include "hvlua.h"

using namespace hv;

static std::string g_probe;
static int l_probe(lua_State* L) {
    size_t len = 0;
    const char* s = luaL_checklstring(L, 1, &len);
    if (!g_probe.empty()) g_probe += ",";
    g_probe.append(s, len);
    return 0;
}

static void test_owned_loop_run_then_free() {
    mqtt_client_t* client = mqtt_client_new(NULL);
    assert(client != NULL);
    htimer_add(client->loop, [](htimer_t* timer) {
        hloop_stop(hevent_loop(timer));
    }, 1, 1);
    mqtt_client_run(client);
    assert(client->loop == NULL);
    assert(client->io == NULL);
    assert(client->timer == NULL);
    mqtt_client_free(client);
}

int main() {
    test_owned_loop_run_then_free();

    hv::EventLoopPtr loop = std::make_shared<hv::EventLoop>();
    lua_State* L = hvlua_state(loop->loop());
    assert(L != NULL);
    lua_pushcfunction(L, l_probe);
    lua_setglobal(L, "probe");

    int ret = hvlua_dostring(loop->loop(),
        "assert(type(hv.mqtt) == 'table', 'hv.mqtt missing')\n"
        "assert(type(hv.mqtt.connect) == 'function', 'hv.mqtt.connect missing')\n"
        "probe('registered')\n"
        "hv.setTimeout(1, function()\n"
        // connect to a port with nothing listening -> (nil, err), not a crash.
        "  local m, merr = hv.mqtt.connect({ host='127.0.0.1', port=1 })\n"
        "  if m == nil then probe('mqtt:'..(merr or '?')) else probe('mqtt:opened?') end\n"
        "  hv.stop()\n"
        "end)\n"
    );
    assert(ret == 0);

    // Guard against a hang: stop the loop after 3s no matter what.
    htimer_add(loop->loop(), [](htimer_t* t){
        hloop_stop(hevent_loop(t));
    }, 3000, 1);

    loop->run();
    loop.reset();

    printf("hv.mqtt result: %s\n", g_probe.c_str());
    // "registered" always; the connect attempt must come back as a non-crashing
    // failure (exact err text is platform/timing dependent, so we only assert it
    // reported back rather than "succeeding").
    assert(g_probe.find("registered") != std::string::npos);
    assert(g_probe.find("mqtt:") != std::string::npos);
    assert(g_probe.find("opened?") == std::string::npos);
    printf("ALL lua_mqtt_test PASSED\n");
    return 0;
}
