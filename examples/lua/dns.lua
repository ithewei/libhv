-- hvlua async DNS example: synchronous-style resolve, non-blocking loop.
-- Run: bin/hvlua examples/lua/dns.lua [host ...]

local hosts = { "localhost", "example.com", "github.com" }
if arg and #arg >= 1 then
    hosts = {}
    for i = 1, #arg do hosts[i] = arg[i] end
end

-- Run each resolve in its own coroutine (via a 1ms timer) so they fire
-- concurrently on the loop; total time ~= the slowest single lookup.
-- (setTimeout requires timeout_ms >= 1; 0 is not a valid timer in libhv)
local pending = #hosts

for _, host in ipairs(hosts) do
    hv.setTimeout(1, function()
        local addrs, err = hv.resolveDns(host)   -- synchronous-style, async underneath
        if err then
            hv.log("resolve", host, "failed:", err)
        else
            hv.log("resolve", host, "->", table.concat(addrs, ", "))
        end
        pending = pending - 1
        if pending == 0 then
            hv.stop()
        end
    end)
end
