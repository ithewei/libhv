-- hv.redis coroutine-synchronous client demo.
-- Usage: hvlua examples/lua/redis_client.lua [host] [port]
local host = arg[1] or "127.0.0.1"
local port = tonumber(arg[2]) or 6379

hv.setTimeout(1, function()
    local r = hv.redis.new({ host = host, port = port, timeout = 3000 })

    -- SET / GET (coroutine-synchronous: these yield until the reply arrives)
    local ok, err = r:set("hv:lua:key", "hello")
    if err then hv.loge("SET failed:", err); hv.stop(); return end
    hv.log("SET ->", ok)

    local v, gerr = r:get("hv:lua:key")
    if gerr then hv.loge("GET failed:", gerr) else hv.log("GET ->", v) end

    -- INCR returns an integer
    local n = r:incr("hv:lua:counter")
    hv.log("INCR ->", n)

    -- arbitrary command via array form; error replies come back as nil,err
    local res, cerr = r:command({ "HSET", "hv:lua:hash", "field", "val" })
    if cerr then hv.log("HSET err:", cerr) else hv.log("HSET ->", res) end

    hv.stop()
end)
