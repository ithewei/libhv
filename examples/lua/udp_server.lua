-- UDP echo server in Lua.
-- Usage: hvlua examples/lua/udp_server.lua [host] [port]
local host = arg[1] or "0.0.0.0"
local port = tonumber(arg[2] or "10530")

local ok, err = hv.udpServer(host, port, function(sock, data, peer)
    hv.log("recv from", peer, ":", data)
    sock:sendto(data)   -- echo back to the last peer
end)

if not ok then
    hv.loge("udpServer failed:", err)
    return
end
hv.log("udp echo server listening on", host .. ":" .. port)
