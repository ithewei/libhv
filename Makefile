include config.mk
include Makefile.vars

MAKEF=$(MAKE) -f Makefile.in
ALL_SRCDIRS=. base util event protocol cpputil evpp http http/client http/server

LIBHV_SRCDIRS = . base util event
LIBHV_HEADERS = hv.h hconfig.h hexport.h
LIBHV_HEADERS += $(BASE_HEADERS) $(UTIL_HEADERS) $(EVENT_HEADERS)

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

default: all
all: libhv examples
examples: hmain_test htimer_test hloop_test \
	nc nmap httpd curl wget consul\
	tcp_echo_server \
	tcp_chat_server \
	tcp_proxy_server \
	udp_echo_server \
	udp_proxy_server \
	http_server_test http_client_test \
	websocket_server_test \
	websocket_client_test \

clean:
	$(MAKEF) clean SRCDIRS="$(ALL_SRCDIRS) examples/nmap examples/httpd examples/consul"
	${RM} examples/*.o
	$(RM) include/hv

prepare:
	$(MKDIR) bin

libhv:
	$(MKDIR) lib
	$(MAKEF) TARGET=$@ TARGET_TYPE="SHARED|STATIC" SRCDIRS="$(LIBHV_SRCDIRS)"
	$(MKDIR) include/hv
	$(CP) $(LIBHV_HEADERS) include/hv

install:
	$(MKDIR) $(INSTALL_INCDIR)
	$(MKDIR) $(INSTALL_LIBDIR)
	$(CP) include/hv/* $(INSTALL_INCDIR)
	$(CP) lib/libhv.*  $(INSTALL_LIBDIR)

hmain_test: prepare
	$(MAKEF) TARGET=$@ SRCDIRS=". base cpputil" SRCS="examples/hmain_test.cpp"

htimer_test: prepare
	$(MAKEF) TARGET=$@ SRCDIRS=". base event" SRCS="examples/htimer_test.c"

hloop_test: prepare
	$(MAKEF) TARGET=$@ SRCDIRS=". base event" SRCS="examples/hloop_test.c"

tcp_echo_server: prepare
	$(MAKEF) TARGET=$@ SRCDIRS=". base event" SRCS="examples/tcp_echo_server.c"

tcp_chat_server: prepare
	$(MAKEF) TARGET=$@ SRCDIRS=". base event" SRCS="examples/tcp_chat_server.c"

tcp_proxy_server: prepare
	$(MAKEF) TARGET=$@ SRCDIRS=". base event" SRCS="examples/tcp_proxy_server.c"

udp_echo_server: prepare
	$(MAKEF) TARGET=$@ SRCDIRS=". base event" SRCS="examples/udp_echo_server.c"

udp_proxy_server: prepare
	$(MAKEF) TARGET=$@ SRCDIRS=". base event" SRCS="examples/udp_proxy_server.c"

nc: prepare
	$(MAKEF) TARGET=$@ SRCDIRS=". base event" SRCS="examples/nc.c"

nmap: prepare
	$(MAKEF) TARGET=$@ SRCDIRS=". base event cpputil examples/nmap" DEFINES="PRINT_DEBUG"

httpd: prepare
	$(RM) examples/httpd/*.o
	$(MAKEF) TARGET=$@ SRCDIRS=". base event util cpputil evpp http http/client http/server examples/httpd"

consul: prepare
	$(MAKEF) TARGET=$@ SRCDIRS=". base event util cpputil evpp http http/client examples/consul" DEFINES="PRINT_DEBUG"

curl: prepare
	$(MAKEF) TARGET=$@ SRCDIRS=". base event util cpputil evpp http http/client" SRCS="examples/curl.cpp"
	# $(MAKEF) TARGET=$@ SRCDIRS=". base event util cpputil evpp http http/client" SRCS="examples/curl.cpp" WITH_CURL=yes

wget: prepare
	$(MAKEF) TARGET=$@ SRCDIRS=". base event util cpputil evpp http http/client" SRCS="examples/wget.cpp"

http_server_test: prepare
	$(MAKEF) TARGET=$@ SRCDIRS=". base event util cpputil evpp http http/server" SRCS="examples/http_server_test.cpp"

http_client_test: prepare
	$(MAKEF) TARGET=$@ SRCDIRS=". base event util cpputil evpp http http/client" SRCS="examples/http_client_test.cpp"

websocket_server_test: prepare
	$(MAKEF) TARGET=$@ SRCDIRS=". base event util cpputil evpp http http/server" SRCS="examples/websocket_server_test.cpp"

websocket_client_test: prepare
	$(MAKEF) TARGET=$@ SRCDIRS=". base event util cpputil evpp http http/client" SRCS="examples/websocket_client_test.cpp"

unittest: prepare
	$(CC)  -g -Wall -O0 -std=c99   -I. -Ibase            -o bin/mkdir_p           unittest/mkdir_test.c         base/hbase.c
	$(CC)  -g -Wall -O0 -std=c99   -I. -Ibase            -o bin/rmdir_p           unittest/rmdir_test.c         base/hbase.c
	$(CC)  -g -Wall -O0 -std=c99   -I. -Ibase            -o bin/date              unittest/date_test.c          base/htime.c
	$(CC)  -g -Wall -O0 -std=c99   -I. -Ibase            -o bin/hatomic_test      unittest/hatomic_test.c       -pthread
	$(CXX) -g -Wall -O0 -std=c++11 -I. -Ibase            -o bin/hatomic_cpp_test  unittest/hatomic_test.cpp     -pthread
	$(CXX) -g -Wall -O0 -std=c++11 -I. -Ibase            -o bin/hthread_test      unittest/hthread_test.cpp     -pthread
	$(CC)  -g -Wall -O0 -std=c99   -I. -Ibase            -o bin/hmutex_test       unittest/hmutex_test.c        base/htime.c   -pthread
	$(CC)  -g -Wall -O0 -std=c99   -I. -Ibase            -o bin/connect_test      unittest/connect_test.c       base/hsocket.c base/htime.c
	$(CC)  -g -Wall -O0 -std=c99   -I. -Ibase            -o bin/socketpair_test   unittest/socketpair_test.c    base/hsocket.c
	$(CXX) -g -Wall -O0 -std=c++11 -I. -Ibase -Icpputil  -o bin/hstring_test      unittest/hstring_test.cpp     cpputil/hstring.cpp
	$(CXX) -g -Wall -O0 -std=c++11 -I. -Ibase -Icpputil  -o bin/ls                unittest/listdir_test.cpp     cpputil/hdir.cpp
	$(CXX) -g -Wall -O0 -std=c++11 -I. -Ibase -Icpputil  -o bin/ifconfig          unittest/ifconfig_test.cpp    cpputil/ifconfig.cpp
	$(CXX) -g -Wall -O0 -std=c++11 -I. -Ibase -Icpputil  -o bin/defer_test        unittest/defer_test.cpp
	$(CXX) -g -Wall -O0 -std=c++11 -I. -Ibase -Icpputil  -o bin/synchronized_test unittest/synchronized_test.cpp -pthread
	$(CXX) -g -Wall -O0 -std=c++11 -I. -Ibase -Icpputil  -o bin/threadpool_test   unittest/threadpool_test.cpp  -pthread
	$(CXX) -g -Wall -O0 -std=c++11 -I. -Ibase -Icpputil  -o bin/objectpool_test   unittest/objectpool_test.cpp  -pthread
	$(CC)  -g -Wall -O0 -std=c99   -I. -Ibase -Iprotocol -o bin/nslookup          unittest/nslookup_test.c      protocol/dns.c
	$(CC)  -g -Wall -O0 -std=c99   -I. -Ibase -Iprotocol -o bin/ping              unittest/ping_test.c          protocol/icmp.c base/hsocket.c base/htime.c -DPRINT_DEBUG
	$(CC)  -g -Wall -O0 -std=c99   -I. -Ibase -Iprotocol -o bin/ftp               unittest/ftp_test.c           protocol/ftp.c  base/hsocket.c
	$(CC)  -g -Wall -O0 -std=c99   -I. -Ibase -Iprotocol -Iutil -o bin/sendmail   unittest/sendmail_test.c      protocol/smtp.c base/hsocket.c util/base64.c

evpp: prepare libhv
	$(CXX) -g -Wall -O0 -std=c++11 -I. -Ibase -Ievent -Icpputil -Ievpp -o bin/EventLoop_test           evpp/EventLoop_test.cpp           -Llib -lhv -lpthread
	$(CXX) -g -Wall -O0 -std=c++11 -I. -Ibase -Ievent -Icpputil -Ievpp -o bin/EventLoopThread_test     evpp/EventLoopThread_test.cpp     -Llib -lhv -lpthread
	$(CXX) -g -Wall -O0 -std=c++11 -I. -Ibase -Ievent -Icpputil -Ievpp -o bin/EventLoopThreadPool_test evpp/EventLoopThreadPool_test.cpp -Llib -lhv -lpthread
	$(CXX) -g -Wall -O0 -std=c++11 -I. -Ibase -Ievent -Icpputil -Ievpp -o bin/TcpServer_test           evpp/TcpServer_test.cpp           -Llib -lhv -lpthread
	$(CXX) -g -Wall -O0 -std=c++11 -I. -Ibase -Ievent -Icpputil -Ievpp -o bin/TcpClient_test           evpp/TcpClient_test.cpp           -Llib -lhv -lpthread
	$(CXX) -g -Wall -O0 -std=c++11 -I. -Ibase -Ievent -Icpputil -Ievpp -o bin/UdpServer_test           evpp/UdpServer_test.cpp           -Llib -lhv -lpthread
	$(CXX) -g -Wall -O0 -std=c++11 -I. -Ibase -Ievent -Icpputil -Ievpp -o bin/UdpClient_test           evpp/UdpClient_test.cpp           -Llib -lhv -lpthread

# UNIX only
webbench: prepare
	$(CC) -o bin/webbench unittest/webbench.c

echo-servers:
	$(CXX) -g -Wall -std=c++11 -o bin/pingpong_client echo-servers/pingpong_client.cpp -lhv -lpthread
	$(CC)  -g -Wall -std=c99   -o bin/libevent_echo   echo-servers/libevent_echo.c     -levent
	$(CC)  -g -Wall -std=c99   -o bin/libev_echo      echo-servers/libev_echo.c        -lev
	$(CC)  -g -Wall -std=c99   -o bin/libuv_echo      echo-servers/libuv_echo.c        -luv
	$(CC)  -g -Wall -std=c99   -o bin/libhv_echo      echo-servers/libhv_echo.c        -lhv
	$(CXX) -g -Wall -std=c++11 -o bin/asio_echo       echo-servers/asio_echo.cpp       -lboost_system -lpthread
	$(CXX) -g -Wall -std=c++11 -o bin/poco_echo       echo-servers/poco_echo.cpp       -lPocoNet -lPocoUtil -lPocoFoundation
#	$(CXX) -g -Wall -std=c++11 -o bin/muduo_echo      echo-servers/muduo_echo.cpp      -lmuduo_net -lmuduo_base -lpthread

.PHONY: clean prepare install libhv examples unittest evpp echo-servers
