// hv/mqtt Promise client demo.
// Usage: hvjs examples/js/mqtt_client.js [host] [port] [topic]

const hv = require("hv");
const mqtt = require("hv/mqtt");

const host = arg[1] || "127.0.0.1";
const port = Number(arg[2] || 1883);
const topic = arg[3] || "hv/js/test";

const client = await mqtt.connect({
    host,
    port,
    id: "hvjs-demo",
    keepalive: 60,
    reconnect: { min_delay: 1000, max_delay: 10000, delay_policy: 2 },
});

hv.log("connected to mqtt", host, port);

client.subscribe(topic, 1);
client.publish(topic, "hello from js", 1);

for (let i = 1; i <= 3; ++i) {
    const msg = await client.recv();
    hv.log("recv ->", msg.topic, msg.payload, "qos", msg.qos);
}

client.disconnect();
