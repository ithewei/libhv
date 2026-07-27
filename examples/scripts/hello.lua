function handle(ctx)
    hv.log("lua request", ctx:method(), ctx:path())
    return ctx:json({
        ok = true,
        path = ctx:path(),
        id = ctx:query("id", "")
    })
end
