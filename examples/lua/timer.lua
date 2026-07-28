-- hvlua timer example
-- Run: bin/hvlua examples/lua/timer.lua

hv.log("timer example start, now =", hv.now())

local n = 0
local id
id = hloop.setInterval(500, function()
    n = n + 1
    hv.log("tick", n)
    if n >= 3 then
        hloop.clearTimer(id)
        hv.log("cleared interval after 3 ticks")
        hloop.setTimeout(200, function()
            hv.log("bye")
            hloop.stop()
        end)
    end
end)
