// hvjs event-loop sleep example.
// Run: bin/hvjs examples/js/sleep.js

const hv = require("hv");

async function worker(name, ms) {
    for (let i = 1; i <= 3; ++i) {
        hv.log(name, "step", i);
        await hv.sleep(ms);
    }
    hv.log(name, "done");
}

await Promise.all([
    worker("A", 300),
    worker("B", 500),
]);

print("sleep example done");
