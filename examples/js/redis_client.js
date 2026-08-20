// hv/redis Promise client demo.
// Usage: hvjs examples/js/redis_client.js [host] [port]

const hv = require("hv");
const redis = require("hv/redis");

const host = arg[1] || "127.0.0.1";
const port = Number(arg[2] || 6379);

const r = redis.new({ host, port, timeout: 3000 });

const ok = await r.set("hv:js:key", "hello");
hv.log("SET ->", ok);

const v = await r.get("hv:js:key");
hv.log("GET ->", v);

const n = await r.incr("hv:js:counter");
hv.log("INCR ->", n);

try {
    const res = await r.command(["HSET", "hv:js:hash", "field", "val"]);
    hv.log("HSET ->", res);
}
catch (e) {
    hv.log("HSET err:", String(e));
}
