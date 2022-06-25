## Done

- base: cross platfrom infrastructure
- event: select/poll/epoll/kqueue/port
- ssl: openssl/guntls/mbedtls
- evpp: c++ EventLoop interface similar to muduo and evpp
- http client/server: include https http1/x http2
- websocket client/server
- mqtt client

## Improving

- IOCP: fix bug, add SSL/TLS support, replace with wepoll?
- wintls: SChannel is so hard :) need help
- Path router: add filter chain, optimized matching via trie?

## Plan

- redis client
- async DNS
- lua binding
- js binding
- hrpc = libhv + protobuf
- rudp: FEC, ARQ, KCP, UDT, QUIC
- kcptun
- have a taste of io_uring
- coroutine
- cppsocket.io
- IM-libhv
- MediaServer-libhv
- GameServer-libhv
