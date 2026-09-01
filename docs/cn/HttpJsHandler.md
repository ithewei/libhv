# Http JS Handler

`HttpScriptHandler` 支持把 `.js` 脚本作为 HTTP 请求处理器执行。JS handler 基于 QuickJS，适合把少量业务逻辑从 C++ 编译周期里解耦出来，并且可以在脚本中使用 `async` / `await` 调用 libhv 的异步能力。

该功能是可选模块，默认不编译。

## 编译

需要 QuickJS 开发库。

Makefile:

```bash
make libhv WITH_JS=yes WITH_HTTP=yes
make http_server_test WITH_JS=yes WITH_HTTP=yes
make unittest WITH_JS=yes WITH_HTTP=yes WITH_REDIS=yes WITH_MQTT=yes
```

如果 QuickJS 安装在自定义路径，可以显式指定：

```bash
make libhv WITH_JS=yes \
    QUICKJS_ROOT=/opt/homebrew/opt/quickjs
```

或：

```bash
make libhv WITH_JS=yes \
    JS_CFLAGS="-I/usr/local/include/quickjs" \
    JS_LIBS="-L/usr/local/lib/quickjs -lquickjs"
```

CMake:

```bash
cmake -S . -B build -DWITH_JS=ON -DWITH_HTTP=ON -DWITH_HTTP_CLIENT=ON -DBUILD_UNITTEST=ON
cmake --build build
```

如果 CMake 没有自动找到 QuickJS：

```bash
cmake -S . -B build -DWITH_JS=ON -DQUICKJS_ROOT=/opt/homebrew/opt/quickjs
```

## 基本用法

C++:

```cpp
#include "HttpServer.h"
#include "HttpJsHandler.h"
#include "HttpScriptHandler.h"

using namespace hv;

int main() {
    HttpService router;
    router.GET("/hello", HttpScriptHandler("scripts/hello.js"));

    HttpServer server;
    server.port = 8080;
    server.service = &router;
    server.run();
    return 0;
}
```

JS:

```js
async function get(ctx) {
    const hv = require("hv");
    await hv.sleep(100);
    return {
        ok: true,
        id: ctx.query("id", ""),
        path: ctx.path()
    };
}
```

如果需要明确指定 JS 引擎，也可以直接使用 `HttpJsHandler("scripts/hello.js")`。推荐用户代码优先使用 `HttpScriptHandler`，这样同一个路由入口可以按脚本后缀分发到不同脚本引擎。

可以通过 `HttpJsHandlerOptions` 调整脚本热加载和运行限制：

```cpp
HttpJsHandlerOptions options;
options.reload_on_change = true;
options.timeout_ms = 30000;              // 单个 HTTP 请求的墙钟预算，0 表示不限制
router.GET("/hello", HttpJsHandler("scripts/hello.js", options));
```

JS runtime 使用内置的内存和栈限制；v1 暂不暴露运行时限制配置。

## 目录映射

`HttpService::Script(path, script_dir)` 可以把 URL 前缀映射到脚本目录，内部同样使用 `HttpScriptHandler`：

```cpp
router.Script("/script/", "scripts");
```

访问 `/script/user?id=42` 时会调用 `scripts/user.js`。访问 `/script/` 时会调用 `scripts/index.js`。如果同时启用了 Lua 和 JS，未带后缀的脚本路径会优先匹配 `.lua`，再匹配 `.js`。

目录映射默认支持 `GET`、`POST`、`PUT`、`DELETE`、`PATCH`。路径中包含 `..` 路径段时返回 `403`。

## ctx API

```js
ctx.method()              // GET/POST/...
ctx.path()                // URL path
ctx.param(name, defaultValue)  // path/query 参数
ctx.query(name, defaultValue)  // ctx.param 的别名
ctx.header(name, defaultValue)
ctx.body()

ctx.status(code)
ctx.setHeader(name, value)
ctx.set_header(name, value)
ctx.text(str)
ctx.json(value)
```

handler 可以直接调用 `ctx.text` / `ctx.json`，也可以返回字符串、数字状态码或 JS 对象：

```js
function post(ctx) {
    ctx.status(201);
    ctx.setHeader("X-From", "js");
    return ctx.text("created");
}
```

## 内置模块

JS handler 提供受控的内置模块，不兼容 Node.js，也不支持 npm 包加载。也就是说，首版不支持 `require("axios")`；请使用 libhv 提供的内置模块。

```js
const hv = require("hv");
hv.version()       // libhv 版本串
hv.log("hello")    // INFO 日志
await hv.sleep(1000)
```

### hv/http

需同时启用 `WITH_HTTP` 和 `WITH_HTTP_CLIENT`。

```js
const http = require("hv/http");

const resp = await http.get("http://127.0.0.1:8080/ping");
// resp: { status, body, headers }

await http.post("http://127.0.0.1:8080/echo", "body", {
    "Content-Type": "text/plain"
});

await http.request("GET", "http://127.0.0.1:8080/ping");
```

### hv/ws

需同时启用 `WITH_HTTP` 和 `WITH_HTTP_CLIENT`。

```js
const wsmod = require("hv/ws");

const ws = await wsmod.connect("ws://127.0.0.1:8888/", {
    timeout: 3000,
    ping_interval: 3000
});
ws.send("hello");
const msg = await ws.recv();
ws.close();
```

`ws.connect()` 使用底层 `TcpClient` 的连接超时；`recv()` 在收到应用消息前保持 pending，连接关闭时会 reject。WebSocket ping/pong 只用于连接健康检查，不会让一个连接健康但没有业务消息的 `recv()` 自动返回。HTTP JS handler 的 `timeout_ms` 是请求级兜底，会结束整个请求并清理仍未完成的 `recv()` 等异步操作。

### hv/redis

需启用 `WITH_REDIS`。

```js
const redis = require("hv/redis");

const r = redis.new({ host: "127.0.0.1", port: 6379, timeout: 3000 });
await r.set("k", "v");
const v = await r.get("k");
const n = await r.incr("c");
const pong = await r.command(["PING"]);
```

Redis 回复映射：string -> string，integer -> number，nil -> null，array -> array，error reply -> rejected Promise。

`redis.new()` 会创建一个 `AsyncRedisClient`。如果脚本在每个 HTTP 请求里调用它，就会产生按请求创建/释放连接的开销；高频路径建议在 C++ 层封装连接池，或后续扩展 JS 绑定提供复用能力。

### hv/mqtt

需启用 `WITH_MQTT`。

```js
const mqtt = require("hv/mqtt");

const client = await mqtt.connect({
    host: "127.0.0.1",
    port: 1883,
    id: "client-1",
    username: "",
    password: "",
    keepalive: 60,
    clean_session: true,
    ssl: false,
    timeout: 3000,
    reconnect: {
        min_delay: 1000,
        max_delay: 10000,
        delay_policy: 2,
        max_retry: 0
    }
});

client.subscribe("topic", 1);
client.publish("topic", "payload", 1, false);
const msg = await client.recv(); // { topic, payload, qos }
client.disconnect();
```

`mqtt.connect()` 的 `timeout` / `connect_timeout` 会设置底层 MQTT client 的连接超时；`recv()` 在收到 `PUBLISH` 前保持 pending，连接关闭时会 reject。MQTT keepalive 用于发现断链，正常 PING/PONG 不会让一个没有业务消息的 `recv()` 自动返回。`reconnect.max_retry = 0` 按 libhv reconnect 语义表示无限重试。

## 异步模型

每个 event loop 会复用一个 QuickJS runtime；每次 HTTP 请求会创建独立 QuickJS context，用于隔离请求级全局对象和 `ctx`。脚本可以返回普通值，也可以返回 Promise；`HttpJsHandler` 会等待 Promise fulfilled/rejected 后再发送 HTTP 响应。`await hv.sleep()`、`await http.get()`、`await ws.recv()`、`await redis.command()`、`await mqtt.connect()` 都在当前 IO 线程的 event loop 上推进，不会阻塞 loop。

`HttpJsHandler` 会缓存脚本文本，并在 `reload_on_change=true` 时根据文件 `mtime` 自动重新读取；每个请求仍使用独立 QuickJS context，因此脚本里的全局变量不会跨请求共享。QuickJS runtime 按 event loop 复用，可以减少每请求初始化 runtime 的开销，但仍会按请求重新执行脚本文本。

默认启用 30 秒请求级 timeout：如果脚本 CPU 循环太久，QuickJS interrupt handler 会中断执行；如果返回的 Promise 长时间不 settle，event loop timer 会结束该 HTTP 请求并清理仍未完成的 libhv 异步操作。错误细节写入日志，HTTP 500 响应体固定为 `javascript handler error`。

当前 JS 字符串绑定通过 `JS_ToCStringLen` 表达数据；`ctx.body()`、`http` response body、`ws.send(..., "binary")`、`mqtt.publish()` 的 payload 仍按字符串处理。需要二进制无损传输时，应等后续版本接入 `ArrayBuffer` / `Uint8Array`。

## 示例

```bash
make http_server_test WITH_JS=yes WITH_HTTP=yes
bin/http_server_test 8080
curl "http://127.0.0.1:8080/script/hello?id=42"
```

也可以直接使用 `hvjs` 运行独立脚本示例：

```bash
make hvjs WITH_JS=yes WITH_HTTP=yes WITH_REDIS=yes WITH_MQTT=yes
bin/hvjs examples/js/sleep.js
bin/hvjs examples/js/http_client.js http://127.0.0.1:8080/ping
```
