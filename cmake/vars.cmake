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
    base/hssl.h
    base/hlog.h
    base/hbuf.h
    base/hmap.h
    base/hstring.h
    base/hfile.h
    base/hdir.h
    base/hurl.h
    base/hscope.h
    base/hthreadpool.h
    base/hobjectpool.h
    base/ifconfig.h
    base/ThreadLocalStorage.h
)

set(UTILS_HEADERS
    utils/base64.h
    utils/md5.h
    utils/sha1.h
    utils/json.hpp
    utils/singleton.h
    utils/iniparser.h
    utils/hendian.h
    utils/hmain.h
)

set(EVENT_HEADERS
    event/hloop.h
    event/nlog.h
    event/nmap.h
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
    http/client/WebSocketClient.h)

set(HTTP_SERVER_HEADERS
    http/server/HttpService.h
    http/server/HttpServer.h
    http/server/WebSocketServer.h
)

set(CONSUL_HEADERS consul/consul.h)
