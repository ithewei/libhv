## 目录结构

```
.
├── client
│   ├── http_client.h   http客户端对外头文件
│   ├── requests.h      模拟python requests api
│   └── axios.h         模拟nodejs axios api
├── httpdef.h           http定义
├── http2def.h          http2定义
├── grpcdef.h           grpc定义
├── HttpParser.h        http解析基类
├── Http1Parser.h       http1解析类
├── Http2Parser.h       http2解析类
├── HttpMessage.h       http请求响应类
├── http_content.h      http Content-Type
├── http_parser.h       http1解析实现
├── multipart_parser.h  multipart解析
└── server
    ├── HttpServer.h    http服务端对外头文件
    ├── HttpHandler.h   http处理类
    ├── FileCache.h     文件缓存类
    ├── http_page.h     http页面构造
    └── HttpService.h   http业务类 (包括api service、web service、indexof service)

```
