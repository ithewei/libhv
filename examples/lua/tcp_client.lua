-- TCP client coroutine echo test
-- Usage: hvlua examples/lua/tcp_client.lua [host] [port]
local host = arg[1] or "127.0.0.1"
local port = tonumber(arg[2] or "10514")

hv.setTimeout(1, function()
    local conn, err = hv.connect(host, port, 3000)
    if err then
        hv.loge("connect failed:", err)
        hv.stop()
        return
    end
    hv.log("connected to", conn:peeraddr(), "fd", conn:fd())

    for i = 1, 3 do
        local msg = "hello " .. i
        conn:write(msg)
        local data, rerr = conn:read()
        if rerr then
            hv.loge("read err:", rerr)
            break
        end
        hv.log("sent:", msg, "| echo:", data)
    end

    conn:close()
    hv.stop()
end)
