# libhv Copilot Instructions

## Project Overview

libhv is a cross-platform (Linux, Windows, macOS, Android, iOS) high-performance C/C++ network library, similar to libevent, libev, and libuv, but with a simpler API and richer protocol support. It provides an event loop with non-blocking IO and timer. The codebase is primarily written in C (C99) with C++ (C++11) wrappers.

## Directory Structure

```
base/       - Core infrastructure: macros, data structures, datetime, threading, process, logging, sockets
event/      - Event loop module (select/poll/epoll/kqueue/iocp/io_uring backends)
evpp/       - C++ wrappers for event loop: EventLoop, TcpServer, TcpClient, UdpServer, UdpClient
ssl/        - SSL/TLS abstraction layer (OpenSSL, GnuTLS, mbedTLS)
http/       - HTTP module
  client/   - HttpClient, AsyncHttpClient, requests.h (Python-requests style), axios.h
  server/   - HttpServer, HttpService (gin-style router), HttpContext, WebSocketServer
util/       - Utility functions: base64, md5, sha1
cpputil/    - C++ utilities: string, file, path, thread pool, JSON, INI parsing
protocol/   - Protocol implementations: ICMP, DNS, FTP, SMTP
mqtt/       - MQTT client
examples/   - Example programs and demos
unittest/   - Unit tests
```

## Build System

libhv supports multiple build systems:

**Makefile (Unix):**
```shell
./configure             # optional: --with-openssl, --with-nghttp2, --with-mqtt, --with-kcp
make
sudo make install
```

**CMake (cross-platform):**
```shell
mkdir build && cd build
cmake .. [options]      # options: -DWITH_OPENSSL=ON, -DWITH_NGHTTP2=ON, -DWITH_MQTT=ON, -DWITH_KCP=ON
cmake --build .
```

**Key CMake options:**
- `BUILD_SHARED` / `BUILD_STATIC` - Library type (both ON by default)
- `BUILD_EXAMPLES` - Build examples (ON by default)
- `BUILD_UNITTEST` - Build unit tests (OFF by default)
- `WITH_OPENSSL` / `WITH_GNUTLS` / `WITH_MBEDTLS` - SSL/TLS backend
- `WITH_NGHTTP2` - HTTP/2 support
- `WITH_EVPP` - C++ wrappers (ON by default)
- `WITH_HTTP` / `WITH_HTTP_SERVER` / `WITH_HTTP_CLIENT` - HTTP support
- `WITH_MQTT` - MQTT support
- `WITH_KCP` - KCP (reliable UDP) support

**Build targets:**
- `make libhv` - Build library only
- `make examples` - Build examples
- `make unittest` - Build unit tests

## Coding Conventions

### Style
- Uses `.clang-format` (based on LLVM style with modifications)
- 4-space indentation, no tabs
- Column limit: 160
- C++11 standard, C99 standard
- Braces: K&R style (opening brace on same line), but `} catch` and `} else` place the closing brace on its own line
- Pointer alignment: right (`char *p`, not `char* p`)
- Short if/loop on single line allowed
- `using namespace hv;` is common in examples

### Naming
- C API: lowercase with `h` or `hv_` prefix (e.g., `hloop_new`, `hio_read`, `hv_malloc`)
- C++ classes: PascalCase in `hv` namespace (e.g., `hv::EventLoop`, `hv::TcpServer`, `hv::HttpServer`)
- Macros/constants: UPPER_CASE with `HV_` or `H` prefix (e.g., `HV_EXPORT`, `HLOOP_FLAG_AUTO_FREE`)
- Callbacks: `typedef void (*hio_cb)(hio_t* io)` pattern for C, `std::function<>` for C++
- Header guards: `HV_FILENAME_H_` format

### Export/Visibility
- `HV_EXPORT` macro for public API symbols
- `HV_INLINE` for static inline functions
- `HV_DEPRECATED` for deprecated APIs
- `BEGIN_EXTERN_C` / `END_EXTERN_C` for C/C++ interop
- `BEGIN_NAMESPACE(ns)` / `END_NAMESPACE(ns)` for namespaces

### Memory Management
- Use `HV_ALLOC`, `HV_FREE`, `SAFE_FREE`, `SAFE_DELETE` macros
- Use `hv_malloc`, `hv_calloc`, `hv_realloc`, `hv_zalloc` for C allocations
- Use `std::shared_ptr` for C++ objects (e.g., `SocketChannelPtr`, `HttpRequestPtr`)

## Key API Patterns

### Event Loop (C API)
```c
hloop_t* loop = hloop_new(HLOOP_FLAG_AUTO_FREE);

// TCP server
hio_t* listenio = hloop_create_tcp_server(loop, host, port, on_accept);

// Timer
htimer_t* timer = htimer_add(loop, on_timer, timeout_ms, repeat_count);

// IO callbacks
hio_setcb_read(io, on_read);
hio_setcb_write(io, on_write);
hio_setcb_close(io, on_close);
hio_read_start(io);

hloop_run(loop);
```

### TCP Server (C++)
```cpp
hv::TcpServer srv;
srv.createsocket(port);
srv.onConnection = [](const hv::SocketChannelPtr& channel) { /* ... */ };
srv.onMessage = [](const hv::SocketChannelPtr& channel, hv::Buffer* buf) { /* ... */ };
srv.setThreadNum(4);
srv.start();
```

### TCP Client (C++)
```cpp
hv::TcpClient cli;
cli.createsocket(port, host);
cli.onConnection = [](const hv::SocketChannelPtr& channel) { /* ... */ };
cli.onMessage = [](const hv::SocketChannelPtr& channel, hv::Buffer* buf) { /* ... */ };
cli.start();
cli.send(data);
```

### HTTP Server (gin-style routing)
```cpp
hv::HttpService router;

// Sync handler: runs on IO thread
router.GET("/ping", [](HttpRequest* req, HttpResponse* resp) {
    return resp->String("pong");
});

// Context handler: runs on IO thread
router.POST("/echo", [](const HttpContextPtr& ctx) {
    return ctx->send(ctx->body(), ctx->type());
});

// Async handler: runs on hv::async thread pool
router.GET("/async", [](const HttpRequestPtr& req, const HttpResponseWriterPtr& writer) {
    writer->Begin();
    writer->WriteHeader("Content-Type", "text/plain");
    writer->WriteBody("hello");
    writer->End();
});

// Static files, reverse proxy
router.Static("/", "/var/www/html");
router.Proxy("/api/", "http://backend:8080/");

hv::HttpServer server(&router);
server.setPort(8080);
server.setThreadNum(4);
server.run();
```

### HTTP Client (Python requests style)
```cpp
#include "requests.h"
auto resp = requests::get("http://example.com/api");
auto resp = requests::post("http://example.com/api", body, headers);
```

### WebSocket Server
```cpp
hv::WebSocketService ws;
ws.onopen = [](const WebSocketChannelPtr& channel, const HttpRequestPtr& req) { /* ... */ };
ws.onmessage = [](const WebSocketChannelPtr& channel, const std::string& msg) { /* ... */ };
ws.onclose = [](const WebSocketChannelPtr& channel) { /* ... */ };

hv::WebSocketServer server(&ws);
server.setPort(9999);
server.run();
```

### WebSocket Client
```cpp
hv::WebSocketClient ws;
ws.onopen = []() { /* ... */ };
ws.onmessage = [](const std::string& msg) { /* ... */ };
ws.onclose = []() { /* ... */ };
ws.open("ws://127.0.0.1:9999/path");
ws.send("hello");
```

## HTTP Handler Chain

The HTTP server processes requests through a handler chain:
```
headerHandler → preprocessor → middleware → processor → postprocessor
```

Where processor resolves as: `pathHandlers → staticHandler → errorHandler`

**Handler types:**
- `http_sync_handler`: `int(HttpRequest*, HttpResponse*)` - Runs on IO thread
- `http_async_handler`: `void(const HttpRequestPtr&, const HttpResponseWriterPtr&)` - Runs on hv::async thread pool
- `http_ctx_handler`: `int(const HttpContextPtr&)` - Runs on IO thread
- `http_state_handler`: `int(const HttpContextPtr&, http_parser_state, const char*, size_t)` - Runs on IO thread

**Return values:**
- `HTTP_STATUS_NEXT` (0) aka `HTTP_STATUS_UNFINISHED` (0): Both are aliases for value 0. Used to mean "continue to next handler" in the chain or "async processing in progress" (response not yet sent)
- `HTTP_STATUS_CLOSE` (-100): Close connection without response
- Any HTTP status code (e.g., 200, 404): Handle done

**Important:** The postprocessor and errorHandler return values should NOT overwrite the main status_code, as this can break async handlers returning `HTTP_STATUS_UNFINISHED`.

## Unpack Modes

libhv provides built-in unpacking modes for TCP streams:
- **FixedLength**: Fixed-length message framing
- **Delimiter**: Delimiter-based framing (e.g., `\r\n`)
- **LengthField**: Length-field based framing (configurable head/body structure)

Configure via `hio_set_unpack()` or `TcpServer::setUnpack()`.

## Thread Safety

- `hio_write()` and `hio_close()` are thread-safe for TCP
- `channel->write()` is thread-safe in C++ API
- `hloop_post_event()` is thread-safe for posting events from other threads
- `TcpServer::broadcast()` is thread-safe
- `hv::async()` runs tasks on a thread pool
- The event loop runs on a single thread; IO callbacks execute on the loop thread

## SSL/TLS

Enable SSL/TLS by building with `WITH_OPENSSL`, `WITH_GNUTLS`, or `WITH_MBEDTLS`:
```cpp
// Server
hssl_ctx_opt_t ssl_opt;
memset(&ssl_opt, 0, sizeof(ssl_opt));
ssl_opt.crt_file = "cert.pem";
ssl_opt.key_file = "key.pem";
server.newSslCtx(&ssl_opt);

// Client
cli.withTLS();

// HTTP server
server.setPort(8080, 8443);  // http_port, https_port
```

## Reconnection

TCP/WebSocket clients support automatic reconnection:
```cpp
reconn_setting_t reconn;
reconn_setting_init(&reconn);
reconn.min_delay = 1000;     // ms
reconn.max_delay = 10000;    // ms
reconn.delay_policy = 2;     // exponential backoff
client.setReconnect(&reconn);
```

## Testing

Unit tests are in the `unittest/` directory. Build with `BUILD_UNITTEST=ON`:
```shell
mkdir build && cd build
cmake .. -DBUILD_UNITTEST=ON
cmake --build .
```

Example/integration tests are in `evpp/*_test.cpp` and `examples/*_test.cpp`.

## Common Pitfalls

1. **DELETE method**: On Windows, `DELETE` is a macro in `<winnt.h>`. Use `Delete()` instead:
   ```cpp
   router.Delete("/resource/:id", handler);  // NOT router.DELETE(...)
   ```

2. **Event loop lifetime**: With `HLOOP_FLAG_AUTO_FREE`, do not call `hloop_free()` manually.

3. **Async handler return**: `invokeHttpHandler` returns `HTTP_STATUS_NEXT` (0) when executing an `http_async_handler` via `hv::async`. Do not treat 0 as an error.

4. **IO thread vs async thread**: Sync handlers and ctx handlers run on the IO thread. Do not perform blocking operations in them. Use `http_async_handler` or `hv::async()` for blocking work.

5. **HttpContext::send()**: Always call `send()` to finalize the response in ctx handlers. It calls `writer->End()` internally.
