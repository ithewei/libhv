include config.mk
include Makefile.vars

MAKEF=$(MAKE) -f Makefile.in
ALL_SRCDIRS=. base ssl event event/kcp util cpputil evpp protocol http http/client http/server mqtt
CORE_SRCDIRS=. base ssl event
ifeq ($(WITH_KCP), yes)
CORE_SRCDIRS += event/kcp
endif

LIBHV_SRCDIRS = $(CORE_SRCDIRS) util
LIBHV_HEADERS = hv.h hconfig.h hexport.h
LIBHV_HEADERS += $(BASE_HEADERS) $(SSL_HEADERS) $(EVENT_HEADERS) $(UTIL_HEADERS)

ifeq ($(WITH_PROTOCOL), yes)
LIBHV_HEADERS += $(PROTOCOL_HEADERS)
LIBHV_SRCDIRS += protocol
endif

ifeq ($(WITH_EVPP), yes)
LIBHV_HEADERS += $(CPPUTIL_HEADERS) $(EVPP_HEADERS)
LIBHV_SRCDIRS += cpputil evpp

ifeq ($(WITH_HTTP), yes)
LIBHV_HEADERS += $(HTTP_HEADERS)
LIBHV_SRCDIRS += http

ifeq ($(WITH_NGHTTP2), yes)
LIBHV_HEADERS += $(HTTP2_HEADERS)
endif

ifeq ($(WITH_HTTP_SERVER), yes)
LIBHV_HEADERS += $(HTTP_SERVER_HEADERS)
LIBHV_SRCDIRS += http/server
endif

ifeq ($(WITH_HTTP_CLIENT), yes)
LIBHV_HEADERS += $(HTTP_CLIENT_HEADERS)
LIBHV_SRCDIRS += http/client
endif

endif
endif

ifeq ($(WITH_MQTT), yes)
LIBHV_HEADERS += $(MQTT_HEADERS)
LIBHV_SRCDIRS += mqtt
endif

default: all

all: libhv examples
	@echo "make all done, please enjoy libhv."

examples: hmain_test htimer_test hloop_test \
	nc nmap tinyhttpd tinyproxyd httpd curl wget wrk consul \
	tcp_client_test \
	tcp_echo_server \
	tcp_chat_server \
	tcp_proxy_server \
	udp_echo_server \
	udp_proxy_server \
	socks5_proxy_server \
	multi-acceptor-processes \
	multi-acceptor-threads \
	one-acceptor-multi-workers \
	http_server_test http_client_test \
	websocket_server_test \
	websocket_client_test \
	mqtt_sub \
	mqtt_pub \
	mqtt_client_test \
	jsonrpc
	@echo "make examples done."

clean:
	$(MAKEF) clean SRCDIRS="$(ALL_SRCDIRS)"
	$(RM) examples/*.o examples/*/*.o
	$(RM) include/hv
	@echo "make clean done."

prepare:
	$(MKDIR) bin

libhv:
	$(MKDIR) lib
	$(MAKEF) TARGET=$@ TARGET_TYPE="SHARED|STATIC" SRCDIRS="$(LIBHV_SRCDIRS)"
	$(MKDIR) include/hv
	$(CP) $(LIBHV_HEADERS) include/hv
	@echo "make libhv done."

install:
	$(MKDIR) $(INSTALL_INCDIR)
	$(MKDIR) $(INSTALL_LIBDIR)
	$(CP) include/hv/* $(INSTALL_INCDIR)
	$(CP) lib/libhv.*  $(INSTALL_LIBDIR)
	$(LDCONFIG)
	@echo "make install done."

uninstall: clean
	$(RM) $(PREFIX)/include/hv
	$(RM) $(PREFIX)/lib/libhv.*
	@echo "make uninstall done."

hmain_test: prepare
	$(MAKEF) TARGET=$@ SRCDIRS=". base cpputil" SRCS="examples/hmain_test.cpp"

htimer_test: prepare
	$(MAKEF) TARGET=$@ SRCDIRS="$(CORE_SRCDIRS)" SRCS="examples/htimer_test.c"

hloop_test: prepare
	$(MAKEF) TARGET=$@ SRCDIRS="$(CORE_SRCDIRS)" SRCS="examples/hloop_test.c"

tcp_client_test: prepare
	$(MAKEF) TARGET=$@ SRCDIRS="$(CORE_SRCDIRS)" SRCS="examples/tcp_client_test.c"

tcp_echo_server: prepare
	$(MAKEF) TARGET=$@ SRCDIRS="$(CORE_SRCDIRS)" SRCS="examples/tcp_echo_server.c"

tcp_chat_server: prepare
	$(MAKEF) TARGET=$@ SRCDIRS="$(CORE_SRCDIRS)" SRCS="examples/tcp_chat_server.c"

tcp_proxy_server: prepare
	$(MAKEF) TARGET=$@ SRCDIRS="$(CORE_SRCDIRS)" SRCS="examples/tcp_proxy_server.c"

udp_echo_server: prepare
	$(MAKEF) TARGET=$@ SRCDIRS="$(CORE_SRCDIRS)" SRCS="examples/udp_echo_server.c"

udp_proxy_server: prepare
	$(MAKEF) TARGET=$@ SRCDIRS="$(CORE_SRCDIRS)" SRCS="examples/udp_proxy_server.c"

socks5_proxy_server: prepare
	$(MAKEF) TARGET=$@ SRCDIRS="$(CORE_SRCDIRS)" SRCS="examples/socks5_proxy_server.c"

multi-acceptor-processes: prepare
	$(MAKEF) TARGET=$@ SRCDIRS="$(CORE_SRCDIRS)" SRCS="examples/multi-thread/multi-acceptor-processes.c"

multi-acceptor-threads: prepare
	$(MAKEF) TARGET=$@ SRCDIRS="$(CORE_SRCDIRS)" SRCS="examples/multi-thread/multi-acceptor-threads.c"

one-acceptor-multi-workers: prepare
	$(MAKEF) TARGET=$@ SRCDIRS="$(CORE_SRCDIRS)" SRCS="examples/multi-thread/one-acceptor-multi-workers.c"

nc: prepare
	$(MAKEF) TARGET=$@ SRCDIRS="$(CORE_SRCDIRS)" SRCS="examples/nc.c"

tinyhttpd: prepare
	$(MAKEF) TARGET=$@ SRCDIRS="$(CORE_SRCDIRS)" SRCS="examples/tinyhttpd.c"

tinyproxyd: prepare
	$(MAKEF) TARGET=$@ SRCDIRS="$(CORE_SRCDIRS)" SRCS="examples/tinyproxyd.c"

nmap: prepare
	$(MAKEF) TARGET=$@ SRCDIRS="$(CORE_SRCDIRS) cpputil examples/nmap" DEFINES="PRINT_DEBUG"

wrk: prepare
	$(MAKEF) TARGET=$@ SRCDIRS="$(CORE_SRCDIRS) util cpputil evpp http" SRCS="examples/wrk.cpp"

httpd: prepare
	$(RM) examples/httpd/*.o
	$(MAKEF) TARGET=$@ SRCDIRS="$(CORE_SRCDIRS) util cpputil evpp http http/client http/server examples/httpd"

consul: prepare
	$(MAKEF) TARGET=$@ SRCDIRS="$(CORE_SRCDIRS) util cpputil evpp http http/client examples/consul" DEFINES="PRINT_DEBUG"

curl: prepare
	$(MAKEF) TARGET=$@ SRCDIRS="$(CORE_SRCDIRS) util cpputil evpp http http/client" SRCS="examples/curl.cpp"
	# $(MAKEF) TARGET=$@ SRCDIRS="$(CORE_SRCDIRS) util cpputil evpp http http/client" SRCS="examples/curl.cpp" WITH_CURL=yes

wget: prepare
	$(MAKEF) TARGET=$@ SRCDIRS="$(CORE_SRCDIRS) util cpputil evpp http http/client" SRCS="examples/wget.cpp"

http_server_test: prepare
	$(MAKEF) TARGET=$@ SRCDIRS="$(CORE_SRCDIRS) util cpputil evpp http http/server" SRCS="examples/http_server_test.cpp"

http_client_test: prepare
	$(MAKEF) TARGET=$@ SRCDIRS="$(CORE_SRCDIRS) util cpputil evpp http http/client" SRCS="examples/http_client_test.cpp"

websocket_server_test: prepare
	$(MAKEF) TARGET=$@ SRCDIRS="$(CORE_SRCDIRS) util cpputil evpp http http/server" SRCS="examples/websocket_server_test.cpp"

websocket_client_test: prepare
	$(MAKEF) TARGET=$@ SRCDIRS="$(CORE_SRCDIRS) util cpputil evpp http http/client" SRCS="examples/websocket_client_test.cpp"

mqtt_sub: prepare
	$(MAKEF) TARGET=$@ SRCDIRS="$(CORE_SRCDIRS) mqtt" SRCS="examples/mqtt/mqtt_sub.c"

mqtt_pub: prepare
	$(MAKEF) TARGET=$@ SRCDIRS="$(CORE_SRCDIRS) mqtt" SRCS="examples/mqtt/mqtt_pub.c"

mqtt_client_test: prepare
	$(MAKEF) TARGET=$@ SRCDIRS="$(CORE_SRCDIRS) mqtt" SRCS="examples/mqtt/mqtt_client_test.cpp"

jsonrpc: jsonrpc_client jsonrpc_server

jsonrpc_client: prepare
	$(MAKEF) TARGET=$@ SRCDIRS="$(CORE_SRCDIRS)" SRCS="examples/jsonrpc/jsonrpc_client.c examples/jsonrpc/cJSON.c"

jsonrpc_server: prepare
	$(RM) examples/jsonrpc/*.o
	$(MAKEF) TARGET=$@ SRCDIRS="$(CORE_SRCDIRS)" SRCS="examples/jsonrpc/jsonrpc_server.c examples/jsonrpc/cJSON.c"

protorpc: protorpc_client protorpc_server

protorpc_protoc:
	bash examples/protorpc/proto/protoc.sh

protorpc_client: prepare protorpc_protoc
	$(MAKEF) TARGET=$@ SRCDIRS="$(CORE_SRCDIRS) cpputil evpp examples/protorpc/generated" \
		SRCS="examples/protorpc/protorpc_client.cpp examples/protorpc/protorpc.c" \
		LIBS="protobuf"

protorpc_server: prepare protorpc_protoc
	$(RM) examples/protorpc/*.o
	$(MAKEF) TARGET=$@ SRCDIRS="$(CORE_SRCDIRS) cpputil evpp examples/protorpc/generated" \
		SRCS="examples/protorpc/protorpc_server.cpp examples/protorpc/protorpc.c" \
		LIBS="protobuf"

unittest: prepare
	$(CC)  -g -Wall -O0 -std=c99   -I. -Ibase            -o bin/rbtree_test       unittest/rbtree_test.c        base/rbtree.c
	$(CC)  -g -Wall -O0 -std=c99   -I. -Ibase            -o bin/hbase_test        unittest/hbase_test.c         base/hbase.c
	$(CC)  -g -Wall -O0 -std=c99   -I. -Ibase            -o bin/mkdir_p           unittest/mkdir_test.c         base/hbase.c
	$(CC)  -g -Wall -O0 -std=c99   -I. -Ibase            -o bin/rmdir_p           unittest/rmdir_test.c         base/hbase.c
	$(CC)  -g -Wall -O0 -std=c99   -I. -Ibase            -o bin/date              unittest/date_test.c          base/htime.c
	$(CC)  -g -Wall -O0 -std=c99   -I. -Ibase            -o bin/hatomic_test      unittest/hatomic_test.c       -pthread
	$(CXX) -g -Wall -O0 -std=c++11 -I. -Ibase            -o bin/hatomic_cpp_test  unittest/hatomic_test.cpp     -pthread
	$(CXX) -g -Wall -O0 -std=c++11 -I. -Ibase            -o bin/hthread_test      unittest/hthread_test.cpp     -pthread
	$(CC)  -g -Wall -O0 -std=c99   -I. -Ibase            -o bin/hmutex_test       unittest/hmutex_test.c        base/htime.c   -pthread
	$(CC)  -g -Wall -O0 -std=c99   -I. -Ibase            -o bin/connect_test      unittest/connect_test.c       base/hsocket.c base/htime.c
	$(CC)  -g -Wall -O0 -std=c99   -I. -Ibase            -o bin/socketpair_test   unittest/socketpair_test.c    base/hsocket.c
	$(CC)  -g -Wall -O0 -std=c99   -I. -Iutil            -o bin/base64            unittest/base64_test.c        util/base64.c
	$(CC)  -g -Wall -O0 -std=c99   -I. -Iutil            -o bin/md5               unittest/md5_test.c           util/md5.c
	$(CC)  -g -Wall -O0 -std=c99   -I. -Iutil            -o bin/sha1              unittest/sha1_test.c          util/sha1.c
	$(CXX) -g -Wall -O0 -std=c++11 -I. -Ibase -Icpputil  -o bin/hstring_test      unittest/hstring_test.cpp     cpputil/hstring.cpp
	$(CXX) -g -Wall -O0 -std=c++11 -I. -Ibase -Icpputil  -o bin/hpath_test        unittest/hpath_test.cpp       cpputil/hpath.cpp
	$(CXX) -g -Wall -O0 -std=c++11 -I. -Ibase -Icpputil  -o bin/hurl_test         unittest/hurl_test.cpp        cpputil/hurl.cpp base/hbase.c
	$(CXX) -g -Wall -O0 -std=c++11 -I. -Ibase -Icpputil  -o bin/ls                unittest/listdir_test.cpp     cpputil/hdir.cpp
	$(CXX) -g -Wall -O0 -std=c++11 -I. -Ibase -Icpputil  -o bin/ifconfig          unittest/ifconfig_test.cpp    cpputil/ifconfig.cpp
	$(CXX) -g -Wall -O0 -std=c++11 -I. -Ibase -Icpputil  -o bin/defer_test        unittest/defer_test.cpp
	$(CXX) -g -Wall -O0 -std=c++11 -I. -Ibase -Icpputil  -o bin/synchronized_test unittest/synchronized_test.cpp -pthread
	$(CXX) -g -Wall -O0 -std=c++11 -I. -Ibase -Icpputil  -o bin/threadpool_test   unittest/threadpool_test.cpp  -pthread
	$(CXX) -g -Wall -O0 -std=c++11 -I. -Ibase -Icpputil  -o bin/objectpool_test   unittest/objectpool_test.cpp  -pthread
	$(CXX) -g -Wall -O0 -std=c++11 -I. -Ibase -Issl -Ievent -Ievpp -Icpputil -Ihttp -Ihttp/client -Ihttp/server -o bin/sizeof_test unittest/sizeof_test.cpp
	$(CC)  -g -Wall -O0 -std=c99   -I. -Ibase -Iprotocol -o bin/nslookup          unittest/nslookup_test.c      protocol/dns.c  base/hsocket.c
	$(CC)  -g -Wall -O0 -std=c99   -I. -Ibase -Iprotocol -o bin/ping              unittest/ping_test.c          protocol/icmp.c base/hsocket.c base/htime.c -DPRINT_DEBUG
	$(CC)  -g -Wall -O0 -std=c99   -I. -Ibase -Iprotocol -o bin/ftp               unittest/ftp_test.c           protocol/ftp.c  base/hsocket.c
	$(CC)  -g -Wall -O0 -std=c99   -I. -Ibase -Iprotocol -Iutil -o bin/sendmail   unittest/sendmail_test.c      protocol/smtp.c base/hsocket.c util/base64.c

run-unittest: unittest
	bash scripts/unittest.sh

check: examples
	bash scripts/check.sh

evpp: prepare libhv
	$(CXX) -g -Wall -O0 -std=c++11 -I. -Ibase -Issl -Ievent -Icpputil -Ievpp -o bin/EventLoop_test           evpp/EventLoop_test.cpp           -Llib -lhv -pthread
	$(CXX) -g -Wall -O0 -std=c++11 -I. -Ibase -Issl -Ievent -Icpputil -Ievpp -o bin/EventLoopThread_test     evpp/EventLoopThread_test.cpp     -Llib -lhv -pthread
	$(CXX) -g -Wall -O0 -std=c++11 -I. -Ibase -Issl -Ievent -Icpputil -Ievpp -o bin/EventLoopThreadPool_test evpp/EventLoopThreadPool_test.cpp -Llib -lhv -pthread
	$(CXX) -g -Wall -O0 -std=c++11 -I. -Ibase -Issl -Ievent -Icpputil -Ievpp -o bin/TimerThread_test         evpp/TimerThread_test.cpp         -Llib -lhv -pthread
	$(CXX) -g -Wall -O0 -std=c++11 -I. -Ibase -Issl -Ievent -Icpputil -Ievpp -o bin/TcpServer_test           evpp/TcpServer_test.cpp           -Llib -lhv -pthread
	$(CXX) -g -Wall -O0 -std=c++11 -I. -Ibase -Issl -Ievent -Icpputil -Ievpp -o bin/TcpClient_test           evpp/TcpClient_test.cpp           -Llib -lhv -pthread
	$(CXX) -g -Wall -O0 -std=c++11 -I. -Ibase -Issl -Ievent -Icpputil -Ievpp -o bin/TcpClientEventLoop_test  evpp/TcpClientEventLoop_test.cpp  -Llib -lhv -pthread
	$(CXX) -g -Wall -O0 -std=c++11 -I. -Ibase -Issl -Ievent -Icpputil -Ievpp -o bin/UdpServer_test           evpp/UdpServer_test.cpp           -Llib -lhv -pthread
	$(CXX) -g -Wall -O0 -std=c++11 -I. -Ibase -Issl -Ievent -Icpputil -Ievpp -o bin/UdpClient_test           evpp/UdpClient_test.cpp           -Llib -lhv -pthread

# UNIX only
webbench: prepare
	$(CC) -o bin/webbench unittest/webbench.c

echo-servers:
	$(CXX) -g -Wall -std=c++11 -O3 -o bin/pingpong_client echo-servers/pingpong_client.cpp -lhv -pthread
	$(CC)  -g -Wall -std=c99   -O3 -o bin/libevent_echo   echo-servers/libevent_echo.c     -levent
	$(CC)  -g -Wall -std=c99   -O3 -o bin/libev_echo      echo-servers/libev_echo.c        -lev
	$(CC)  -g -Wall -std=c99   -O3 -o bin/libuv_echo      echo-servers/libuv_echo.c        -luv
	$(CC)  -g -Wall -std=c99   -O3 -o bin/libhv_echo      echo-servers/libhv_echo.c        -lhv
	$(CXX) -g -Wall -std=c++11 -O3 -o bin/asio_echo       echo-servers/asio_echo.cpp       -lboost_system -pthread
	$(CXX) -g -Wall -std=c++11 -O3 -o bin/poco_echo       echo-servers/poco_echo.cpp       -lPocoNet -lPocoUtil -lPocoFoundation
#	$(CXX) -g -Wall -std=c++11 -O3 -o bin/muduo_echo      echo-servers/muduo_echo.cpp      -lmuduo_net -lmuduo_base -pthread

echo-benchmark: echo-servers
	bash echo-servers/benchmark.sh

.PHONY: clean prepare install uninstall libhv examples unittest evpp echo-servers
