-- hvlua coroutine sleep example: synchronous-style, non-blocking.
-- Run: bin/hvlua examples/lua/sleep.lua

hv.log("sleep example start")

-- This runs inside a coroutine, so hloop.sleep() suspends WITHOUT blocking the
-- event loop: the two "workers" below interleave rather than run serially.

hloop_workers_done = 0

local function worker(name, ms)
    for i = 1, 3 do
        hv.log(name, "step", i)
        hloop.sleep(ms)   -- looks blocking, actually yields to the loop
    end
    hv.log(name, "done")
    hloop_workers_done = hloop_workers_done + 1
    if hloop_workers_done == 2 then
        hloop.stop()
    end
end

-- start two workers via timers so both run on the loop
-- (setTimeout requires timeout_ms >= 1; 0 is not a valid timer in libhv)
hloop.setTimeout(1, function() worker("A", 300) end)
hloop.setTimeout(1, function() worker("B", 500) end)
