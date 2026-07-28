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

如果需要明确指定 Lua 引擎，也可以直接使用 `LuaHandler("scripts/hello.lua")`。推荐用户代码优先使用 `HttpScriptHandler`，这样后续增加 JS/Python 等脚本引擎时不用改路由注册代码。

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

首版只提供少量宿主能力：

```lua
hv.log(...)
hv.now()
```

暂不暴露 `hv.event_loop`、TCP/HTTP client、Redis 等能力，避免脚本直接操作底层事件循环。后续可以按业务需要增加受控的 `hv.redis`、`hv.http` 等模块。

## 热更新

`HttpScriptHandler` 当前会把 `.lua` 文件转给 `LuaHandler`。`LuaHandler` 会记录脚本文件的 `mtime`。每次请求前，如果文件被修改，会重新加载脚本。

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
