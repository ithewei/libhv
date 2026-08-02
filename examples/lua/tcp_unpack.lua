-- TCP unpack test: length-field framing round-trip against a raw echo server.
-- Usage: hvlua examples/lua/tcp_unpack.lua [host] [port]
-- Frame: [flags:1][length:4 BE][body]  (length = body length)
local host = arg[1] or "127.0.0.1"
local port = tonumber(arg[2] or "10514")

local function frame(body)
    local n = #body
    local len = string.char(
        (n >> 24) & 0xff, (n >> 16) & 0xff, (n >> 8) & 0xff, n & 0xff)
    return string.char(0) .. len .. body    -- flags=0, 4-byte BE length, body
end

hv.setTimeout(1, function()
    local conn, err = hv.connect(host, port, 3000)
    if err then hv.loge("connect:", err); hv.stop(); return end

    conn:setUnpack({
        mode = "length_field",
        body_offset = 5,            -- head = flags(1) + length(4)
        length_field_offset = 1,
        length_field_bytes = 4,
        length_field_coding = "be",
    })

    -- send two frames back-to-back; unpack must split them into 2 reads
    conn:write(frame("hello"))
    conn:write(frame("world!!"))

    for i = 1, 2 do
        local pkt, rerr = conn:read()   -- returns exactly one full frame
        if rerr then hv.loge("read:", rerr); break end
        local body = pkt:sub(6)          -- skip 5-byte head
        hv.log("packet", i, "len", #pkt, "body", body)
    end

    conn:close()
    hv.stop()
end)
