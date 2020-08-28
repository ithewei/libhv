include config.mk
include Makefile.vars

MAKEF=$(MAKE) -f Makefile.in
ALL_SRCDIRS=. base utils event protocol http http/client http/server consul examples

LIBHV_SRCDIRS = . base utils event
LIBHV_HEADERS = hv.h hconfig.h hexport.h
LIBHV_HEADERS += $(BASE_HEADERS) $(UTILS_HEADERS) $(EVENT_HEADERS)

ifeq ($(WITH_PROTOCOL), yes)
LIBHV_HEADERS += $(PROTOCOL_HEADERS)
LIBHV_SRCDIRS += protocol
endif

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
ifeq ($(WITH_CONSUL), yes)
LIBHV_HEADERS += $(CONSUL_HEADERS)
LIBHV_SRCDIRS += consul
endif
endif
endif

default: all
all: libhv examples
examples: hmain_test htimer_test hloop_test tcp udp nc nmap httpd curl consul_cli

clean:
	$(MAKEF) clean SRCDIRS="$(ALL_SRCDIRS)"
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
	$(CP) include/hv/* $(INSTALL_INCDIR)
	$(CP) lib/libhv.a lib/libhv.so $(INSTALL_LIBDIR)

hmain_test: prepare
	$(MAKEF) TARGET=$@ SRCDIRS=". base utils" SRCS="examples/hmain_test.cpp"

htimer_test: prepare
	$(MAKEF) TARGET=$@ SRCDIRS=". base event" SRCS="examples/htimer_test.c"

hloop_test: prepare
	$(MAKEF) TARGET=$@ SRCDIRS=". base event" SRCS="examples/hloop_test.c"

tcp: prepare
	$(MAKEF) TARGET=$@ SRCDIRS=". base event" SRCS="examples/tcp.c"

udp: prepare
	$(MAKEF) TARGET=$@ SRCDIRS=". base event" SRCS="examples/udp.c"

nc: prepare
	$(MAKEF) TARGET=$@ SRCDIRS=". base event" SRCS="examples/nc.c"

nmap: prepare
ifeq ($(OS), Windows)
	# for nmap on Windows platform, recommand EVENT_POLL, not EVENT_IOCP
	$(MAKEF) TARGET=$@ SRCDIRS=". base event" SRCS="examples/nmap.cpp" DEFINES="PRINT_DEBUG EVENT_POLL"
else
	$(MAKEF) TARGET=$@ SRCDIRS=". base event" SRCS="examples/nmap.cpp" DEFINES="PRINT_DEBUG"
endif

httpd: prepare
	$(RM) examples/httpd/*.o
	$(MAKEF) TARGET=$@ SRCDIRS=". base utils event http http/server examples/httpd"

curl: prepare
	$(MAKEF) TARGET=$@ SRCDIRS="$(CURL_SRCDIRS)" SRCDIRS=". base utils http http/client" SRCS="examples/curl.cpp"
	# $(MAKEF) TARGET=$@ SRCDIRS="$(CURL_SRCDIRS)" SRCDIRS=". base utils http http/client" SRCS="examples/curl.cpp" WITH_CURL=yes DEFINES="CURL_STATICLIB"

consul_cli: prepare
	$(MAKEF) TARGET=$@ SRCDIRS=". base utils http http/client consul" SRCS="examples/consul_cli.cpp" DEFINES="PRINT_DEBUG"

unittest: prepare
	$(CC)  -g -Wall -std=c99   -I. -Ibase            -o bin/mkdir_p           unittest/mkdir_test.c         base/hbase.c
	$(CC)  -g -Wall -std=c99   -I. -Ibase            -o bin/rmdir_p           unittest/rmdir_test.c         base/hbase.c
	$(CC)  -g -Wall -std=c99   -I. -Ibase            -o bin/date              unittest/date_test.c          base/htime.c
	$(CC)  -g -Wall -std=c99   -I. -Ibase            -o bin/hatomic_test      unittest/hatomic_test.c       -pthread
	$(CXX) -g -Wall -std=c++11 -I. -Ibase            -o bin/hatomic_cpp_test  unittest/hatomic_test.cpp     -pthread
	$(CC)  -g -Wall -std=c99   -I. -Ibase            -o bin/hmutex_test       unittest/hmutex_test.c        base/htime.c -pthread
	$(CC)  -g -Wall -std=c99   -I. -Ibase            -o bin/connect_test      unittest/connect_test.c       base/hsocket.c base/htime.c
	$(CC)  -g -Wall -std=c99   -I. -Ibase            -o bin/socketpair_test   unittest/socketpair_test.c    base/hsocket.c
	$(CXX) -g -Wall -std=c++11 -I. -Ibase            -o bin/defer_test        unittest/defer_test.cpp
	$(CXX) -g -Wall -std=c++11 -I. -Ibase            -o bin/synchronized_test unittest/synchronized_test.cpp -pthread
	$(CXX) -g -Wall -std=c++11 -I. -Ibase            -o bin/hstring_test      unittest/hstring_test.cpp     base/hstring.cpp
	$(CXX) -g -Wall -std=c++11 -I. -Ibase            -o bin/threadpool_test   unittest/threadpool_test.cpp  -pthread
	$(CXX) -g -Wall -std=c++11 -I. -Ibase            -o bin/objectpool_test   unittest/objectpool_test.cpp  -pthread
	$(CXX) -g -Wall -std=c++11 -I. -Ibase            -o bin/ls                unittest/listdir_test.cpp     base/hdir.cpp
	$(CXX) -g -Wall -std=c++11 -I. -Ibase            -o bin/ifconfig          unittest/ifconfig_test.cpp    base/ifconfig.cpp
	$(CC)  -g -Wall -std=c99   -I. -Ibase -Iprotocol -o bin/nslookup          unittest/nslookup_test.c      protocol/dns.c
	$(CC)  -g -Wall -std=c99   -I. -Ibase -Iprotocol -o bin/ping              unittest/ping_test.c          protocol/icmp.c base/hsocket.c base/htime.c -DPRINT_DEBUG
	$(CC)  -g -Wall -std=c99   -I. -Ibase -Iprotocol -o bin/ftp               unittest/ftp_test.c           protocol/ftp.c  base/hsocket.c
	$(CC)  -g -Wall -std=c99   -I. -Ibase -Iutils -Iprotocol -o bin/sendmail  unittest/sendmail_test.c      protocol/smtp.c base/hsocket.c utils/base64.c

# UNIX only
webbench: prepare
	$(CC) -o bin/webbench unittest/webbench.c

echo-servers:
	$(CC)  -g -Wall -std=c99   -o bin/libevent_echo echo-servers/libevent_echo.c -levent
	$(CC)  -g -Wall -std=c99   -o bin/libev_echo    echo-servers/libev_echo.c    -lev
	$(CC)  -g -Wall -std=c99   -o bin/libuv_echo    echo-servers/libuv_echo.c    -luv
	$(CC)  -g -Wall -std=c99   -o bin/libhv_echo    echo-servers/libhv_echo.c    -lhv
	$(CXX) -g -Wall -std=c++11 -o bin/asio_echo     echo-servers/asio_echo.cpp   -lboost_system
	$(CXX) -g -Wall -std=c++11 -o bin/poco_echo     echo-servers/poco_echo.cpp   -lPocoNet -lPocoUtil -lPocoFoundation
	$(CXX) -g -Wall -std=c++11 -o bin/muduo_echo    echo-servers/muduo_echo.cpp  -lmuduo_net -lmuduo_base -lpthread

.PHONY: clean prepare libhv install examples hmain_test htimer_test hloop_test tcp udp nc nmap httpd curl consul_cli unittest webbench echo-servers
