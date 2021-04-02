# modules
# include icmp dns ftp smtp
WITH_PROTOCOL=no

WITH_HTTP=yes
WITH_HTTP_SERVER=yes
WITH_HTTP_CLIENT=yes

# WITH_CONSUL need WITH_HTTP_CLIENT=yes
WITH_CONSUL=no

# features
# base/hsocket.c: replace gethostbyname with getaddrinfo
ENABLE_IPV6=no
# base/hsocket.h: Unix Domain Socket
ENABLE_UDS=no
# base/RAII.cpp: Windows MiniDumpWriteDump
ENABLE_WINDUMP=no
# http/http_content.h: KeyValue,QueryParams,MultiPart
USE_MULTIMAP=no

# dependencies
# for http/client
WITH_CURL=no
# for http2
WITH_NGHTTP2=no
# for SSL/TLS
WITH_OPENSSL=no
WITH_MBEDTLS=no
