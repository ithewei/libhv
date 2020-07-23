# see Makefile.vars

set(BASE_HEADERS
    base/hplatform.h
    base/hdef.h
    base/hatomic.h
    base/hversion.h
    base/hbase.h
    base/hsysinfo.h
    base/hproc.h
    base/hmath.h
    base/htime.h
    base/herr.h
    base/hlog.h
    base/hmutex.h
    base/hthread.h
    base/hsocket.h
    base/hbuf.h
    base/hurl.h
    base/hgui.h
    base/ssl_ctx.h
    base/hmap.h
    base/hstring.h
    base/hvar.h
    base/hobj.h
    base/hfile.h
    base/hdir.h
    base/hscope.h
    base/hthreadpool.h
    base/hobjectpool.h
    base/ifconfig.h
)

set(UTILS_HEADERS
    utils/base64.h
    utils/md5.h
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
    http/http_content.h
    http/HttpMessage.h
    http/HttpParser.h
)

set(HTTP_CLIENT_HEADERS http/client/http_client.h)
set(HTTP_SERVER_HEADERS http/server/HttpService.h http/server/HttpServer.h)
set(CONSUL_HEADERS consul/consul.h)
