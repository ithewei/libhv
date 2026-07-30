# Lua Binding

libhv 提供一套 Lua 绑定，让 Lua 脚本直接驱动 libhv 的事件循环与异步网络能力。核心特点是**用同步写法表达异步 IO**：脚本里 `local data = conn:read()`、`local resp = hv.http.get(url)`、`local v = r:get(k)` 读起来都是阻塞式直觉，但底层通过协程 `yield → 异步回调 → resume`，事件循环全程不阻塞（与 OpenResty 同模型）。

该功能是可选模块，默认不编译。

## 编译

需要 Lua 5.3 或更新版本的开发库。

Makefile：

```bash
./configure --with-lua --with-http --with-redis --with-mqtt
make libhv
make hvlua        # 独立 Lua 运行时
make unittest     # 编译 lua 相关单测
```

CMake：

```bash
cmake -S . -B build -DWITH_LUA=ON -DWITH_HTTP=ON -DWITH_REDIS=ON -DWITH_MQTT=ON
cmake --build build
```

分层说明：`hloop.*` 定时器、`hv.tcpClient/tcpServer/udpClient/udpServer`、`hv.resolveDns`、`hv.json`、`hv.log` 只依赖纯 C 的 event 层，`WITH_LUA` 即可用；`hv.http` / `hv.ws` 需要 `WITH_HTTP`，`hv.redis` 需要 `WITH_REDIS`，`hv.mqtt` 需要 `WITH_MQTT`。未开启对应模块时，Lua 里 `hv.http` / `hv.redis` / `hv.mqtt` 为 `nil`。

## 运行脚本

独立运行时 `hvlua`：

```bash
bin/hvlua examples/lua/timer.lua
bin/hvlua examples/lua/tcp_client.lua 127.0.0.1 10514
```

`examples/lua/` 下有 timer / sleep / dns / tcp / udp / http / ws / redis / mqtt 各场景的示例脚本。

## 核心模型

- **每个 loop 线程一个 lua_State**，存放在 `hloop_t` 上，loop 销毁时关闭。
- **协程同步写法**：每个任务（一段脚本 / 每个 HTTP 请求 / 每个接入连接）跑在独立协程里；可挂起的绑定（`conn:read`、`hv.sleep`、`hv.http.get`、`r:get` 等）内部 `yield`，异步结果回来后在**同一 loop 线程**上 `resume`，脚本从挂起点继续。
- **无锁**：单 loop 线程 + 协作式协程，任意时刻只有一个协程在执行，其余停在各自 yield 点。注意跨 yield 点的全局状态可能被其它任务穿插修改（与 OpenResty 同模型）。
- **协作式调度**：纯 CPU 死循环会阻塞该 loop 线程。

## 返回值约定（统一）

- 成功：返回 Lua 原生值（string / integer / boolean / table）。
- 无结果（Redis nil 回复、DNS 无记录）：返回 `nil`。
- 失败：返回 `nil, "错误消息"`（第二个返回值为错误串），脚本用 `local v, err = ...; if err then ... end` 处理。

---

## hv 通用工具

```lua
hv.version()                 -- libhv 版本串，如 "1.3.4"
hv.log(...)                  -- 以 tab 连接参数，info 级日志（logi 的别名）
hv.logd(...) / hv.logi(...) / hv.logw(...) / hv.loge(...)  -- debug/info/warn/error

hv.json.encode(tbl)          -- table -> json 字符串
hv.json.decode(str)          -- json 字符串 -> table
```

## 定时器与协程 sleep

```lua
local id  = hv.setTimeout(1000, function() print("once") end)   -- 返回句柄
local id2 = hv.setInterval(500, function() print("tick") end)
hv.clearTimer(id)

hv.sleep(1000)               -- 协程同步：挂起当前协程 1000ms，loop 不阻塞

hv.run()                     -- 运行当前 loop（独立运行时用；HTTP handler 内不需要）
hv.stop()                    -- 停止当前 loop
```

## hv.resolveDns（协程同步）

```lua
local addrs, err = hv.resolveDns("example.com")
-- addrs: { "93.184.216.34", ... } ; 失败: nil, err
```

## TCP / UDP（event 层，协程同步）

命名对齐 C++ 类 `hv::TcpClient` / `TcpServer` / `UdpClient` / `UdpServer`；`hv.connect` 是 `hv.tcpClient` 的别名，`hv.listen` 是 `hv.tcpServer` 的别名。

### TCP 客户端

```lua
local conn, err = hv.tcpClient(host, port [, timeout_ms])   -- 别名 hv.connect；协程同步，连上或失败
conn:write("hello")                     -- 非阻塞，进写队列，即发即走
local data, err = conn:read()           -- 协程同步：挂起直到有数据；对端关闭返回 nil,"closed"
conn:close()
conn:fd()                               -- fd 或 -1
conn:peeraddr()                         -- "ip:port"
```

拆包读（文本协议 / 二进制协议）：

```lua
local line = conn:readline()            -- 读到 '\n'（含）
local data = conn:readuntil("\n")       -- 读到单字节分隔符（含）
local buf  = conn:readbytes(16)         -- 读满 16 字节

-- 设置一次后，conn:read() 每次返回一个完整的包（二进制协议最常用）
conn:setUnpack({
    mode = "length_field",              -- none | fixed | delimiter | length_field
    package_max_length = 1 << 21,
    -- length_field 模式:
    body_offset = 5, length_field_offset = 1,
    length_field_bytes = 4, length_field_coding = "be",  -- be | le | varint | asn1
    length_adjustment = 0,
    -- delimiter 模式: delimiter = "\r\n"
    -- fixed 模式:     fixed_length = 16
})
```

### TCP 服务端

`on_conn(conn)` 在每个新连接的独立协程里被调用，因此可在里面用同步写法。

```lua
hv.tcpServer("0.0.0.0", 8080, function(conn)   -- 别名 hv.listen
    while true do
        local data, err = conn:read()
        if err then break end               -- 连接关闭
        conn:write(data)                     -- echo
    end
end)
```

### UDP

UDP 无连接，`sock` 复用 conn 对象。

```lua
local sock = hv.udpClient("127.0.0.1", 8080)
sock:sendto("ping")
local data, peer = sock:recvfrom()           -- 协程同步，返回数据 + 对端地址

hv.udpServer("0.0.0.0", 8080, function(sock, data, peer)
    sock:sendto("pong")
end)
```

## hv.http（协程同步，需 WITH_HTTP）

```lua
local resp, err = hv.http.get("http://127.0.0.1:8080/ping")
-- resp: { status = 200, body = "...", headers = { ... } }
local resp2 = hv.http.post("http://.../echo", "body", { ["Content-Type"] = "text/plain" })
local resp3 = hv.http.put("http://.../item", "body")
local resp4 = hv.http.delete("http://.../item")
local resp5 = hv.http.request("GET", url [, body [, headers]])
```

## hv.ws（WebSocket，协程同步，需 WITH_HTTP）

WebSocket 是消息驱动的，收到的消息缓存在收件箱，`ws:recv()` 挂起直到有一条消息（或连接关闭返回 `nil,"closed"`）。

```lua
local ws, err = hv.ws.connect("ws://127.0.0.1:8888/path" [, headers])
ws:send("hello")                    -- 文本帧
ws:send(payload, "binary")          -- 二进制帧
local msg, err = ws:recv()          -- 协程同步：挂起直到收到一条消息
ws:close()
```

## hv.redis（协程同步，需 WITH_REDIS）

```lua
local r = hv.redis.new({ host = "127.0.0.1", port = 6379, auth = "", db = 0, timeout = 3000 })

r:set("k", "v")                     -- 语法糖
local v = r:get("k")                -- bulk -> string / nil
local n = r:incr("c")               -- integer
r:del("k") / r:decr("c") / r:expire("k", 60) / r:exists("k")

-- 任意命令：变参或数组表两种形态
local pong = r:command("PING")
local ret  = r:command({ "HSET", "u:1", "name", "tom" })
```

回复到 Lua 值的映射：string -> string，integer -> integer，nil 回复 -> nil，array -> 表（1 起始，其中嵌套的 nil 元素用 `false` 占位），error 回复 -> `nil, "错误消息"`。

> `hv.redis` 用 `new` 而非 `connect`：它是纯构造，连接是懒发起 + 断线重连，命令可在未连接时排队；这与 `hv.ws.connect` / `hv.mqtt.connect` 挂起到握手完成才返回的语义不同。

## hv.mqtt（协程同步，需 WITH_MQTT）

MQTT 是消息驱动的，`m:recv()` 挂起直到 broker 推来一条 PUBLISH。

```lua
local m, err = hv.mqtt.connect({
    host = "127.0.0.1", port = 1883,
    id = "client-1", username = "", password = "",
    keepalive = 60, clean_session = true, ssl = false,
})                                   -- 协程同步：挂起到 CONNACK 或失败

m:subscribe("topic", 1)              -- 返回 mid
m:publish("topic", "payload", 1, 0)  -- topic, payload, qos, retain -> mid
m:unsubscribe("topic")
local msg = m:recv()                 -- { topic =, payload =, qos = } ; 关闭返回 nil,"closed"
m:disconnect()
```

## HTTP Lua Handler

在 HTTP 服务端里用 Lua 脚本处理请求（`handle(ctx)`），请求在 IO 线程的协程里执行，脚本内可用上述同步写法调用异步 client。详见 [HttpLuaHandler.md](HttpLuaHandler.md)。
