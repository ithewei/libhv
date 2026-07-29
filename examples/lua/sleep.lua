-- hvlua coroutine sleep example: synchronous-style, non-blocking.
-- Run: bin/hvlua examples/lua/sleep.lua

hv.log("sleep example start")

-- This runs inside a coroutine, so hv.sleep() suspends WITHOUT blocking the
-- event loop: the two "workers" below interleave rather than run serially.

workers_done = 0

local function worker(name, ms)
    for i = 1, 3 do
        hv.log(name, "step", i)
        hv.sleep(ms)   -- looks blocking, actually yields to the loop
    end
    hv.log(name, "done")
    workers_done = workers_done + 1
    if workers_done == 2 then
        hv.stop()
    end
end

-- start two workers via timers so both run on the loop
-- (setTimeout requires timeout_ms >= 1; 0 is not a valid timer in libhv)
hv.setTimeout(1, function() worker("A", 300) end)
hv.setTimeout(1, function() worker("B", 500) end)
