# AGENTS.md

This file provides guidance to agents when working with code in this repository.

## Project Overview

libhv is a cross-platform C/C++ network library providing event-loop with non-blocking IO and timer. Core is C99, high-level wrappers are C++11. Compatible with gcc4.8+, MSVC2015+, clang. Supports Linux, Windows, macOS, Android, iOS, BSD, Solaris.

## Build Commands

### Makefile (primary, Unix)
```bash
./configure --with-openssl --with-http --with-mqtt --with-kcp  # configure options
make libhv          # build library only (shared + static)
make                # build library + examples
make examples       # build all example programs
make unittest       # compile unit tests
make evpp           # build C++ evpp tests (requires libhv built first)
make clean          # clean build artifacts
sudo make install   # install to /usr/local/include/hv and /usr/local/lib
```

### CMake (cross-platform)
```bash
mkdir build && cd build
cmake .. -DWITH_OPENSSL=ON -DWITH_HTTP=ON -DBUILD_EXAMPLES=ON
cmake --build .
# Windows: cmake .. -G "Visual Studio 17 2022" -A x64
```

### Bazel
```bash
bazel build libhv
```

### Package Managers
```bash
vcpkg install libhv    # vcpkg
xrepo install libhv    # xmake
```

## Testing

```bash
make unittest                 # compile all unit tests
make run-unittest             # compile and run unit tests (calls scripts/unittest.sh)
bash scripts/unittest.sh      # run pre-built unit tests
make check                    # integration test: builds httpd, runs HTTP checks (scripts/check.sh)
```

Run a single unit test directly:
```bash
bin/rbtree_test    # or any test binary in bin/
```

Run evpp C++ tests (link against libhv):
```bash
make evpp
bin/TcpServer_test
bin/EventLoop_test
```

## Key Configuration Options

Build flags via `./configure` or CMake `-D` options (see `config.ini` for defaults):

| Makefile flag | CMake flag | Purpose |
|---|---|---|
| `--with-openssl` | `-DWITH_OPENSSL=ON` | SSL/TLS via OpenSSL |
| `--with-gnutls` | `-DWITH_GNUTLS=ON` | SSL/TLS via GnuTLS |
| `--with-mbedtls` | `-DWITH_MBEDTLS=ON` | SSL/TLS via mbedTLS |
| `--with-nghttp2` | `-DWITH_NGHTTP2=ON` | HTTP/2 support |
| `--with-kcp` | `-DWITH_KCP=ON` | KCP reliable UDP |
| `--with-mqtt` | `-DWITH_MQTT=ON` | MQTT client |
| `--with-protocol` | `-DWITH_PROTOCOL=ON` | ICMP, DNS, FTP, SMTP |
| `--with-evpp` | `-DWITH_EVPP=ON` | C++ wrappers (default: yes) |
| `--without-evpp` | `-DWITH_EVPP=OFF` | Pure C build, no C++ |
| `--enable-uds` | `-DENABLE_UDS=ON` | Unix Domain Socket |
| `--with-io-uring` | `-DWITH_IO_URING=ON` | io_uring event backend (Linux 5.1+) |

## Architecture

```
Application / Examples
    │
    ├── http/server, http/client   HTTP/WebSocket/gRPC (C++)
    ├── mqtt/                      MQTT client (C)
    ├── protocol/                  ICMP, DNS, FTP, SMTP (C)
    │
    ├── evpp/                      C++ wrappers: TcpServer, TcpClient, UdpServer, EventLoop
    │
    ├── event/                     Core event loop: hloop, hio, htimer
    │   └── backends: epoll (Linux), kqueue (macOS/BSD), iocp/wepoll (Windows), io_uring (Linux 5.1+), select (fallback)
    │   └── kcp/                   KCP reliable UDP transport
    │
    ├── ssl/                       Unified SSL interface (OpenSSL / GnuTLS / mbedTLS / platform)
    │
    ├── base/                      Platform abstraction, sockets, threads, logging, data structures
    ├── util/                      C utilities (base64, md5, sha1)
    └── cpputil/                   C++ utilities (string, path, file, json, threadpool, ini parser)
```

**Layering rules**: `base/` has no dependencies on other modules. `event/` depends on `base/` and `ssl/`. `evpp/` wraps `event/`. `http/` depends on `evpp/`. Higher layers are optional and controlled by build flags.

**Public API entry point**: `hv.h` includes all base headers. Module-specific headers (e.g., `HttpServer.h`, `TcpServer.h`, `mqtt_client.h`) are the primary include for each feature. Installed headers go to `include/hv/`.

**Key types**: `hloop_t` (event loop), `hio_t` (IO handle), `htimer_t` (timer) in the C API. `EventLoop`, `TcpServer`, `TcpClient`, `Channel` in the C++ API. `HttpRequest`, `HttpResponse`, `HttpService` for HTTP.

## Code Style

- **Formatting**: `.clang-format` — LLVM-based, 4-space indent, 160 column limit, pointer right-aligned, `catch`/`else` on new line (custom brace wrapping), no include sorting.
- **C API naming**: `h` prefix for functions (`hloop_new`, `hio_read`), `_t` suffix for types (`hloop_t`, `hio_t`), UPPERCASE macros (`HV_EXPORT`).
- **C++ API naming**: PascalCase classes (`EventLoop`, `TcpServer`), `hv` namespace.
- **File naming**: lowercase with underscores (`.c` for C, `.cpp` for C++).
- **Platform-specific code**: isolated via `hplatform.h` and `#ifdef` conditional compilation.

## CI

GitHub Actions (`.github/workflows/CI.yml`): builds and tests on Linux (with OpenSSL+nghttp2+KCP+MQTT), Windows (CMake + VS2022), macOS, Android (NDK cross-compile), iOS (Xcode cross-compile). Benchmark workflow runs echo-server throughput and HTTP performance comparisons.
