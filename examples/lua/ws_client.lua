-- hv.ws coroutine-synchronous WebSocket client demo.
-- Usage: hvlua examples/lua/ws_client.lua [url]
local url = arg[1] or "ws://127.0.0.1:8888/"

hv.setTimeout(1, function()
    local ws, err = hv.ws.connect(url)
    if err then
        hv.loge("connect failed:", err)
        hv.stop()
        return
    end
    hv.log("connected to", url)

    ws:send("hello from lua")

    -- recv() suspends the coroutine until a message arrives (or the peer closes,
    -- in which case it returns nil, "closed"). Loop until closed.
    for i = 1, 3 do
        local msg, rerr = ws:recv()
        if rerr then hv.log("recv:", rerr); break end
        hv.log("recv ->", msg)
        ws:send("echo " .. i)
    end

    ws:close()
    hv.stop()
end)
