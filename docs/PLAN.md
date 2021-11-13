## Done

- event: select/poll/epoll/kqueue/port
- evpp: c++ EventLoop interface similar to muduo and evpp
- http client/server: include https http1/x http2
- websocket client/server

## Improving

- IOCP: fix bug, add SSL/TLS support, replace with wepoll?
- wintls: SChannel is so hard :) need help
- Path router: add filter chain, optimized matching via trie?

## Plan

- mqtt client
- redis client
- lua binding
- js binding
- hrpc = libhv + protobuf
- reliable udp: FEC, ARQ, KCP, UDT, QUIC
- have a taste of io_uring
