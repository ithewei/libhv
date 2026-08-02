-- UDP client in Lua: sendto + recvfrom (coroutine-synchronous).
-- Usage: hvlua examples/lua/udp_client.lua [host] [port]
local host = arg[1] or "127.0.0.1"
local port = tonumber(arg[2] or "10530")

hv.setTimeout(1, function()
    local sock = hv.udpClient(host, port)
    for i = 1, 3 do
        local msg = "ping " .. i
        sock:sendto(msg)
        local data, peer = sock:recvfrom()
        if not data then
            hv.loge("recvfrom err:", peer)
            break
        end
        hv.log("sent:", msg, "| recv from", peer, ":", data)
    end
    sock:close()
    hv.stop()
end)
