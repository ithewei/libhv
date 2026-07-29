# Http Lua Handler

`HttpScriptHandler` 允许 `HttpService` 调用脚本里的 `handle(ctx)` 方法处理 HTTP 请求。当前支持 `.lua` 脚本，适合把少量业务逻辑从 C++ 编译周期里解耦出来：修改脚本后无需重新编译服务，下一次请求会自动加载新脚本。

该功能是可选模块，默认不编译。

## 编译

需要 Lua 5.3 或更新版本的开发库。

Makefile:

```bash
make libhv WITH_LUA=yes
make http_server_test WITH_LUA=yes
make unittest WITH_LUA=yes
```

如果系统没有 `pkg-config`，或者 Lua 安装在自定义路径，可以显式指定：

```bash
make libhv WITH_LUA=yes \
    LUA_CFLAGS="-I/usr/local/include/lua" \
    LUA_LIBS="-L/usr/local/lib -llua"
```

CMake:

```bash
cmake -S . -B build -DWITH_LUA=ON -DBUILD_EXAMPLES=ON -DBUILD_UNITTEST=ON
cmake --build build
```

## 基本用法

C++:

```cpp
#include "HttpServer.h"
#include "HttpScriptHandler.h"

using namespace hv;

int main() {
    HttpService router;
    router.GET("/hello", HttpScriptHandler("scripts/hello.lua"));

    HttpServer server;
    server.port = 8080;
    server.service = &router;
    server.run();
    return 0;
}
```

Lua:

```lua
function handle(ctx)
    local id = ctx:query("id", "")
    return ctx:json({
        ok = true,
        id = id,
        path = ctx:path()
    })
end
```

如果需要明确指定 Lua 引擎，也可以直接使用 `HttpLuaHandler("scripts/hello.lua")`。推荐用户代码优先使用 `HttpScriptHandler`，这样后续增加 JS/Python 等脚本引擎时不用改路由注册代码。

## 目录映射

`HttpService::Script(path, script_dir)` 可以把 URL 前缀映射到脚本目录，内部同样使用 `HttpScriptHandler`：

```cpp
router.Script("/script/", "scripts");
```

访问 `/script/user?id=42` 时会调用 `scripts/user.lua`。访问 `/script/` 时会调用 `scripts/index.lua`。当前目录映射只自动补 `.lua` 后缀。

目录映射默认支持 `GET`、`POST`、`PUT`、`DELETE`、`PATCH`。路径中包含 `..` 路径段时返回 `403`。

## ctx API

首版只暴露 HTTP handler 必需的最小 API：

```lua
ctx:method()              -- GET/POST/...
ctx:path()                -- URL path
ctx:param(name, default)  -- query/restful params
ctx:query(name, default)  -- alias of param
ctx:header(name, default)
ctx:body()

ctx:status(code)
ctx:set_header(name, value)
ctx:text(str)
ctx:json(table)
```

`handle(ctx)` 可以直接调用 `ctx:text` / `ctx:json` 返回，也可以返回字符串、数字状态码或 Lua table：

```lua
function handle(ctx)
    ctx:status(201)
    ctx:set_header("X-From", "lua")
    return ctx:text("created")
end
```

## hv API

脚本运行在所属 IO 线程的 **协程** 里，因此可以用 **同步写法** 调用异步能力：调用会挂起当前请求的协程、把控制权交还事件循环，结果就绪后在同一线程恢复，全程不阻塞 loop。

所有脚本可用能力都挂在统一的全局 `hv` 表下：

```lua
hv.version()                -- libhv 版本串, 如 "1.3.4"
hv.log(...)                 -- 日志 (INFO), 等价 hv.logi
hv.logd(...) / hv.logi(...) / hv.logw(...) / hv.loge(...)  -- debug/info/warn/error
hv.json.encode(tbl)         -- table -> json string
hv.json.decode(str)         -- json string -> table

hv.setTimeout(ms, fn)       -- 定时器 (返回句柄)
hv.setInterval(ms, fn)
hv.clearTimer(handle)
hv.sleep(ms)                -- 协程同步 sleep: 挂起当前协程 ms 毫秒, 不阻塞 loop
hv.resolveDns(host)         -- 协程同步 DNS 解析: 返回 { ip, ... } 或 nil, err
hv.run() / hv.stop()        -- 运行/停止当前线程的 event loop (独立脚本用; HTTP handler 内不需要)

-- TCP/UDP (协程同步, event 层, 仅当前 loop)
local conn, err = hv.connect(host, port [, timeout_ms])  -- TCP 客户端
hv.tcpServer(host, port, function(conn) ... end)          -- 每连接一个协程
local sock = hv.udpClient(host, port)
hv.udpServer(host, port, function(sock, data, peer) ... end)
--   conn: conn:read() / conn:readline() / conn:readuntil(d) / conn:readbytes(n)
--         conn:setUnpack(opts) / conn:write(s) / conn:close() / conn:fd() / conn:peeraddr()
--   sock: sock:sendto(s) / sock:recvfrom() -> data,peer / sock:close()
```

`conn:setUnpack(opts)` 让后续 `conn:read()` 每次返回一个完整的包(参考 libhv `hio_set_unpack`)：

```lua
conn:setUnpack({
    mode = "length_field",       -- none|fixed|delimiter|length_field
    body_offset = 5, length_field_offset = 1,
    length_field_bytes = 4, length_field_coding = "be",  -- be|le|varint|asn1
    -- delimiter 模式: delimiter = "\r\n" ; fixed 模式: fixed_length = 16
})
```

示例（handler 内部“同步”写法，实际异步，loop 不阻塞）：

```lua
function handle(ctx)
    local addrs, err = hv.resolveDns("example.com")
    if err then
        ctx:status(502)
        return ctx:json({ ok = false, error = err })
    end
    return ctx:json({ ok = true, addrs = addrs })
end
```

多个请求会在同一 IO 线程上并发交错执行：某个请求在 `hv.resolveDns` / `hv.sleep` 处挂起时，同线程的其它请求会继续推进。注意协作式调度的语义——跨越挂起点不要对全局状态做原子性假设。

> `hv.setTimeout` / `hv.resolveDns` 对应 `event/` 层能力，`hv.log` / `hv.version` 对应 `base/` 层，`hv.json` 对应 `cpputil/`；实现分别在 `lua/hvlua_event.c`、`lua/hvlua_base.c`、`lua/hvlua_json.cpp`。TCP/HTTP client、Redis 等高层 client 绑定后续按 `WITH_LUA` / `WITH_REDIS` 等开关编入。

## 热更新

`HttpScriptHandler` 当前会把 `.lua` 文件转给 `HttpLuaHandler`。`HttpLuaHandler` 会记录脚本文件的 `mtime`。每次请求前，如果文件被修改，会重新加载脚本。

重新加载失败时：

- 如果已有旧版本脚本，继续使用旧版本。
- 如果首次加载失败，返回 `500`。

这能避免线上脚本语法错误直接打断已有服务。

## 示例

```bash
make http_server_test WITH_LUA=yes
bin/http_server_test 8080
curl "http://127.0.0.1:8080/lua/hello?id=42"
curl "http://127.0.0.1:8080/script/hello?id=42"
```
