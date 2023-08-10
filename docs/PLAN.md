## Done

- base: cross platfrom infrastructure
- event: select/poll/epoll/wepoll/kqueue/port
- ssl: openssl/gnutls/mbedtls/wintls/appletls
- rudp: KCP
- evpp: c++ EventLoop interface similar to muduo and evpp
- http client/server: include https http1/x http2
- websocket client/server
- mqtt client

## Improving

- Path router: optimized matching via trie?
- FileCache use LRUCache

## Plan

- redis client
- async DNS
- lua binding
- js binding
- hrpc = libhv + protobuf
- rudp: FEC, ARQ, UDT, QUIC
- kcptun
- have a taste of io_uring
- coroutine
- cppsocket.io
- IM-libhv
- MediaServer-libhv
- GameServer-libhv
