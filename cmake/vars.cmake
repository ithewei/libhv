# see Makefile.vars

set(BASE_HEADERS
    base/hplatform.h
    base/hdef.h
    base/hatomic.h
    base/herr.h
    base/htime.h
    base/hmath.h
    base/hbase.h
    base/hversion.h
    base/hsysinfo.h
    base/hproc.h
    base/hthread.h
    base/hmutex.h
    base/hsocket.h
    base/hlog.h
    base/hbuf.h
    base/hendian.h
)

set(SSL_HEADERS
    ssl/hssl.h
)

set(EVENT_HEADERS
    event/hloop.h
    event/nlog.h
)

set(UTIL_HEADERS
    util/base64.h
    util/md5.h
    util/sha1.h
)

set(CPPUTIL_HEADERS
    cpputil/hmap.h
    cpputil/hstring.h
    cpputil/hfile.h
    cpputil/hdir.h
    cpputil/hurl.h
    cpputil/hmain.h
    cpputil/hscope.h
    cpputil/hthreadpool.h
    cpputil/hobjectpool.h
    cpputil/ifconfig.h
    cpputil/iniparser.h
    cpputil/json.hpp
    cpputil/singleton.h
    cpputil/ThreadLocalStorage.h
)

set(EVPP_HEADERS
    evpp/Buffer.h
    evpp/Callback.h
    evpp/Channel.h
    evpp/Event.h
    evpp/EventLoop.h
    evpp/EventLoopThread.h
    evpp/EventLoopThreadPool.h
    evpp/Status.h
    evpp/TcpClient.h
    evpp/TcpServer.h
    evpp/UdpClient.h
    evpp/UdpServer.h
)

set(PROTOCOL_HEADERS
    protocol/icmp.h
    protocol/dns.h
    protocol/ftp.h
    protocol/smtp.h
)

set(HTTP_HEADERS
    http/httpdef.h
    http/http2def.h
    http/grpcdef.h
    http/wsdef.h
    http/http_content.h
    http/HttpMessage.h
    http/HttpParser.h
    http/WebSocketParser.h
    http/WebSocketChannel.h
)

set(HTTP_CLIENT_HEADERS
    http/client/http_client.h
    http/client/requests.h
    http/client/axios.h
    http/client/WebSocketClient.h)

set(HTTP_SERVER_HEADERS
    http/server/HttpServer.h
    http/server/HttpService.h
    http/server/HttpContext.h
    http/server/HttpResponseWriter.h
    http/server/WebSocketServer.h
)
