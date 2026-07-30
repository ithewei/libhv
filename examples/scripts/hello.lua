function get(ctx)
    hv.log("lua get", ctx:path())
    return ctx:json({
        ok = true,
        method = "GET",
        path = ctx:path(),
        id = ctx:query("id", "")
    })
end

function post(ctx)
    hv.log("lua post", ctx:path())
    return ctx:text("POST " .. ctx:body())
end

function handle(ctx)
    hv.log("lua fallback", ctx:method(), ctx:path())
    return ctx:json({
        ok = true,
        method = ctx:method(),
        path = ctx:path()
    })
end
