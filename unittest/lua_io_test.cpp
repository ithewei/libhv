/*
 * lua_io_test — unit test for the libhv Lua event-layer IO binding (hvlua_event.c).
 *
 * Runs deterministically on a single loop, no external processes: a Lua TCP
 * echo server and a client run on the same loop; the client drives the checks
 * and stops the loop. Covers:
 *   1. hv.tcpServer + hv.connect + conn:read/write echo round-trip
 *   2. conn:setUnpack length_field framing (one read == one packet)
 *   3. hv.udpServer + hv.udpClient sendto/recvfrom
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <string>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include "hloop.h"
#include "hvlua.h"

static std::string g_probe;

static int l_probe(lua_State* L) {
    size_t len = 0;
    const char* s = luaL_checklstring(L, 1, &len);
    if (!g_probe.empty()) g_probe += ",";
    g_probe.append(s, len);
    return 0;
}

static void run_script(const char* code) {
    g_probe.clear();
    hloop_t* loop = hloop_new(HLOOP_FLAG_AUTO_FREE | HLOOP_FLAG_QUIT_WHEN_NO_ACTIVE_EVENTS);
    assert(loop != NULL);
    lua_State* L = hvlua_state(loop);
    assert(L != NULL);
    lua_pushcfunction(L, l_probe);
    lua_setglobal(L, "probe");
    int ret = hvlua_dostring(loop, code);
    assert(ret == 0);
    hloop_run(loop);   // AUTO_FREE closes the lua_State on return
}

static void test_tcp_echo() {
    run_script(
        "hv.tcpServer('127.0.0.1', 20701, function(conn)\n"
        "  while true do\n"
        "    local d, e = conn:read()\n"
        "    if e then break end\n"
        "    conn:write(d)\n"
        "  end\n"
        "end)\n"
        "hv.setTimeout(1, function()\n"
        "  local c, err = hv.connect('127.0.0.1', 20701, 2000)\n"
        "  if err then probe('connect-err') hv.stop() return end\n"
        "  c:write('ping')\n"
        "  local d = c:read()\n"
        "  probe(d)\n"
        "  c:close()\n"
        "  hv.stop()\n"
        "end)\n"
    );
    assert(g_probe == "ping");
    printf("  test_tcp_echo OK\n");
}

static void test_tcp_unpack() {
    // server echoes raw bytes; client frames length_field packets and expects
    // one read == one packet.
    run_script(
        "hv.tcpServer('127.0.0.1', 20702, function(conn)\n"
        "  while true do\n"
        "    local d, e = conn:read()\n"
        "    if e then break end\n"
        "    conn:write(d)\n"
        "  end\n"
        "end)\n"
        "local function frame(body)\n"
        "  local n = #body\n"
        "  return string.char(0, (n>>24)&255,(n>>16)&255,(n>>8)&255,n&255) .. body\n"
        "end\n"
        "hv.setTimeout(1, function()\n"
        "  local c = hv.connect('127.0.0.1', 20702, 2000)\n"
        "  c:setUnpack({mode='length_field', body_offset=5, length_field_offset=1,\n"
        "               length_field_bytes=4, length_field_coding='be'})\n"
        "  c:write(frame('aa'))\n"
        "  c:write(frame('bbbb'))\n"
        "  for i=1,2 do\n"
        "    local pkt = c:read()\n"
        "    probe(pkt:sub(6))\n"
        "  end\n"
        "  c:close()\n"
        "  hv.stop()\n"
        "end)\n"
    );
    assert(g_probe == "aa,bbbb");
    printf("  test_tcp_unpack OK\n");
}

static void test_udp_echo() {
    run_script(
        "hv.udpServer('127.0.0.1', 20703, function(sock, data, peer)\n"
        "  sock:sendto(data)\n"
        "end)\n"
        "hv.setTimeout(1, function()\n"
        "  local s = hv.udpClient('127.0.0.1', 20703)\n"
        "  s:sendto('hello-udp')\n"
        "  local d = s:recvfrom()\n"
        "  probe(d)\n"
        "  s:close()\n"
        "  hv.stop()\n"
        "end)\n"
    );
    assert(g_probe == "hello-udp");
    printf("  test_udp_echo OK\n");
}

int main() {
    test_tcp_echo();
    test_tcp_unpack();
    test_udp_echo();
    printf("ALL lua_io_test PASSED\n");
    return 0;
}
