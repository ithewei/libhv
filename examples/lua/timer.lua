-- hvlua timer example
-- Run: bin/hvlua examples/lua/timer.lua

hv.log("timer example start, libhv", hv.version())

local n = 0
local id
id = hv.setInterval(500, function()
    n = n + 1
    hv.log("tick", n)
    if n >= 3 then
        hv.clearTimer(id)
        hv.log("cleared interval after 3 ticks")
        hv.setTimeout(200, function()
            hv.log("bye")
            hv.stop()
        end)
    end
end)
