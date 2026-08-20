// hv/ws Promise WebSocket client demo.
// Usage: hvjs examples/js/ws_client.js [url]

const hv = require("hv");
const wsmod = require("hv/ws");

const url = arg[1] || "ws://127.0.0.1:8888/";
const ws = await wsmod.connect(url);

hv.log("connected to", url);
ws.send("hello from js");

for (let i = 1; i <= 3; ++i) {
    const msg = await ws.recv();
    hv.log("recv ->", msg);
    ws.send("echo " + i);
}

ws.close();
