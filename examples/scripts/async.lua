-- Async HTTP handler demo: synchronous-style code, non-blocking loop.
--
-- The handler resolves a hostname via hv.resolveDns (which yields the request
-- coroutine to the event loop and resumes when the answer arrives) and can also
-- hv.sleep without blocking other requests on the same IO thread.
--
-- Register in C++:  router.GET("/async", HttpScriptHandler("examples/scripts/async.lua"))
-- Try:              curl "http://127.0.0.1:8080/async?host=example.com"

function handle(ctx)
    local host = ctx:query("host", "example.com")

    -- optional artificial delay to demonstrate non-blocking concurrency
    local delay = tonumber(ctx:query("delay", "0"))
    if delay > 0 then
        hv.sleep(delay)
    end

    local addrs, err = hv.resolveDns(host)
    if err then
        ctx:status(502)
        return ctx:json({ ok = false, host = host, error = err })
    end

    return ctx:json({
        ok = true,
        host = host,
        addrs = addrs,
    })
end
