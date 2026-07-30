-- hv.mqtt coroutine-synchronous MQTT client demo.
-- Usage: hvlua examples/lua/mqtt_client.lua [host] [port] [topic]
local host  = arg[1] or "127.0.0.1"
local port  = tonumber(arg[2]) or 1883
local topic = arg[3] or "hv/lua/test"

hv.setTimeout(1, function()
    -- connect() suspends until CONNACK (or fails with nil, err)
    -- reconnect is optional; given it enables auto-reconnect.
    local m, err = hv.mqtt.connect({
        host = host, port = port, id = "hvlua-demo", keepalive = 60,
        reconnect = { min_delay = 1000, max_delay = 10000, delay_policy = 2 },
    })
    if err then
        hv.loge("connect failed:", err)
        hv.stop()
        return
    end
    hv.log("connected to mqtt", host, port)

    m:subscribe(topic, 1)
    m:publish(topic, "hello from lua", 1)

    -- recv() suspends until a PUBLISH arrives; returns { topic, payload, qos }.
    for i = 1, 3 do
        local msg, rerr = m:recv()
        if rerr then hv.log("recv:", rerr); break end
        hv.log("recv ->", msg.topic, msg.payload, "qos", msg.qos)
    end

    m:disconnect()
    hv.stop()
end)
