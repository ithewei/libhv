// hv/http Promise client demo.
// Usage: hvjs examples/js/http_client.js [url]

const hv = require("hv");
const http = require("hv/http");

const url = arg[1] || "http://127.0.0.1:18090/ping";

const resp = await http.get(url);
hv.log("GET", url, "->", resp.status, "body:", resp.body);

const second = await http.get(url);
hv.log("2nd GET ->", second.status);
