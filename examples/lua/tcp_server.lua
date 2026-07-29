-- TCP echo server in Lua (coroutine per connection).
-- Usage: hvlua examples/lua/tcp_server.lua [host] [port]
local host = arg[1] or "0.0.0.0"
local port = tonumber(arg[2] or "10520")

local ok, err = hv.tcpServer(host, port, function(conn)
    hv.log("accepted", conn:peeraddr())
    while true do
        local data, rerr = conn:read()
        if rerr then
            hv.log("conn closed:", conn:peeraddr())
            break
        end
        conn:write(data)   -- echo
    end
end)

if not ok then
    hv.loge("tcpServer failed:", err)
    return
end
hv.log("echo server listening on", host .. ":" .. port)
-- no hv.stop(): run until killed (server keeps the loop alive)
