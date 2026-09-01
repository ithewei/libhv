function get(ctx) {
    const hv = require("hv");
    hv.log("js get", ctx.path());
    return {
        ok: true,
        method: "GET",
        path: ctx.path(),
        id: ctx.query("id", "")
    };
}

function post(ctx) {
    const hv = require("hv");
    hv.log("js post", ctx.path());
    return ctx.text("POST " + ctx.body());
}

function handle(ctx) {
    const hv = require("hv");
    hv.log("js fallback", ctx.method(), ctx.path());
    return {
        ok: true,
        method: ctx.method(),
        path: ctx.path()
    };
}
