include Makefile.vars

MAKEF=$(MAKE) -f Makefile.in
TMPDIR=tmp

default: all
all: libhv examples
examples: test timer loop tcp udp nc nmap httpd curl consul_cli

clean:
	$(MAKEF) clean SRCDIRS=". base utils event http http/client http/server protocol examples $(TMPDIR)"

prepare:
	$(MKDIR) -p $(TMPDIR) lib bin
	$(RM) base/RAII.o

libhv: prepare
	$(RM) include
	$(MAKEF) TARGET=$@ TARGET_TYPE=SHARED SRCDIRS=". base utils event http http/client http/server protocol"
	$(MAKEF) TARGET=$@ TARGET_TYPE=STATIC SRCDIRS=". base utils event http http/client http/server protocol"
	$(MKDIR) include
	$(CP) $(INSTALL_HEADERS) include

install:
	$(MKDIR) -p $(INSTALL_INCDIR)
	$(CP) include/* $(INSTALL_INCDIR)
	$(CP) lib/libhv.a lib/libhv.so $(INSTALL_LIBDIR)

test: prepare
	$(RM) $(TMPDIR)/*.o $(TMPDIR)/*.h $(TMPDIR)/*.c $(TMPDIR)/*.cpp
	cp main.cpp.tmpl $(TMPDIR)/main.cpp
	$(MAKEF) TARGET=$@ SRCDIRS=". base utils $(TMPDIR)"

timer: prepare
	$(RM) $(TMPDIR)/*.o $(TMPDIR)/*.h $(TMPDIR)/*.c $(TMPDIR)/*.cpp
	cp examples/timer.c $(TMPDIR)
	$(MAKEF) TARGET=$@ SRCDIRS=". base event $(TMPDIR)"

loop: prepare
	$(RM) $(TMPDIR)/*.o $(TMPDIR)/*.h $(TMPDIR)/*.c $(TMPDIR)/*.cpp
	cp examples/loop.c $(TMPDIR)
	$(MAKEF) TARGET=$@ SRCDIRS=". base event $(TMPDIR)"

tcp: prepare
	$(RM) $(TMPDIR)/*.o $(TMPDIR)/*.h $(TMPDIR)/*.c $(TMPDIR)/*.cpp
	cp examples/tcp.c $(TMPDIR)
	$(MAKEF) TARGET=$@ SRCDIRS=". base event $(TMPDIR)"

udp: prepare
	$(RM) $(TMPDIR)/*.o $(TMPDIR)/*.h $(TMPDIR)/*.c $(TMPDIR)/*.cpp
	cp examples/udp.c $(TMPDIR)
	$(MAKEF) TARGET=$@ SRCDIRS=". base event $(TMPDIR)"

nc: prepare
	$(RM) $(TMPDIR)/*.o $(TMPDIR)/*.h $(TMPDIR)/*.c $(TMPDIR)/*.cpp
	cp examples/nc.c $(TMPDIR)
	$(MAKEF) TARGET=$@ SRCDIRS=". base event $(TMPDIR)"

nmap: prepare
	$(RM) $(TMPDIR)/*.o $(TMPDIR)/*.h $(TMPDIR)/*.c $(TMPDIR)/*.cpp
	cp examples/nmap.cpp $(TMPDIR)
ifeq ($(OS), Windows)
	# for nmap on Windows platform, recommand EVENT_POLL, not EVENT_IOCP
	$(MAKEF) TARGET=$@ SRCDIRS=". base event $(TMPDIR)" DEFINES="$(DEFINES) PRINT_DEBUG EVENT_POLL"
else
	$(MAKEF) TARGET=$@ SRCDIRS=". base event $(TMPDIR)" DEFINES="$(DEFINES) PRINT_DEBUG"
endif

httpd: prepare
	$(RM) $(TMPDIR)/*.o $(TMPDIR)/*.h $(TMPDIR)/*.c $(TMPDIR)/*.cpp
	cp examples/httpd.cpp examples/http_api_test.h $(TMPDIR)
	$(MAKEF) TARGET=$@ SRCDIRS=". base utils event http http/server $(TMPDIR)"

curl: prepare
	$(RM) $(TMPDIR)/*.o $(TMPDIR)/*.h $(TMPDIR)/*.c $(TMPDIR)/*.cpp
	cp examples/curl.cpp $(TMPDIR)
	$(MAKEF) TARGET=$@ SRCDIRS="$(CURL_SRCDIRS)" SRCDIRS=". base utils http http/client $(TMPDIR)"
	#$(MAKEF) TARGET=$@ SRCDIRS="$(CURL_SRCDIRS)" SRCDIRS=". base utils http http/client $(TMPDIR)" DEFINES="$(DEFINES) WITH_CURL CURL_STATICLIB"

consul_cli: prepare
	$(RM) $(TMPDIR)/*.o $(TMPDIR)/*.h $(TMPDIR)/*.c $(TMPDIR)/*.cpp
	cp examples/consul_cli.cpp $(TMPDIR)
	$(MAKEF) TARGET=$@ SRCDIRS=". base utils event http http/client consul $(TMPDIR)" DEFINES="PRINT_DEBUG"

unittest: prepare
	$(CC)  -g -Wall -std=c99   -I. -Ibase            -o bin/hmutex     unittest/hmutex_test.c        -pthread
	$(CC)  -g -Wall -std=c99   -I. -Ibase            -o bin/connect    unittest/connect_test.c       base/hsocket.c
	$(CC)  -g -Wall -std=c99   -I. -Ibase            -o bin/socketpair unittest/socketpair_test.c    base/hsocket.c
	$(CXX) -g -Wall -std=c++11 -I. -Ibase            -o bin/defer      unittest/defer_test.cpp
	$(CXX) -g -Wall -std=c++11 -I. -Ibase            -o bin/threadpool unittest/threadpool_test.cpp  -pthread
	$(CXX) -g -Wall -std=c++11 -I. -Ibase            -o bin/objectpool unittest/objectpool_test.cpp  -pthread
	$(CXX) -g -Wall -std=c++11 -I. -Ibase            -o bin/ls         unittest/listdir_test.cpp     base/hdir.cpp base/hbase.c
	$(CXX) -g -Wall -std=c++11 -I. -Ibase -Iutils    -o bin/ifconfig   unittest/ifconfig_test.cpp    utils/ifconfig.cpp
	$(CC)  -g -Wall -std=c99   -I. -Ibase -Iprotocol -o bin/nslookup   unittest/nslookup_test.c      protocol/dns.c
	$(CC)  -g -Wall -std=c99   -I. -Ibase -Iprotocol -o bin/ping       unittest/ping_test.c          protocol/icmp.c base/hsocket.c base/htime.c -DPRINT_DEBUG
	$(CC)  -g -Wall -std=c99   -I. -Ibase -Iprotocol -o bin/ftp        unittest/ftp_test.c           protocol/ftp.c  base/hsocket.c
	$(CC)  -g -Wall -std=c99   -I. -Ibase -Iutils -Iprotocol -o bin/sendmail  unittest/sendmail_test.c  protocol/smtp.c base/hsocket.c utils/base64.c

# UNIX only
webbench: prepare
	$(CC) -o bin/webbench unittest/webbench.c

echo-servers:
	$(CC)  -g -Wall -std=c99   -o bin/libevent_echo echo-servers/libevent_echo.c -levent
	$(CC)  -g -Wall -std=c99   -o bin/libev_echo    echo-servers/libev_echo.c    -lev
	$(CC)  -g -Wall -std=c99   -o bin/libuv_echo    echo-servers/libuv_echo.c    -luv
	$(CC)  -g -Wall -std=c99   -o bin/libhv_echo    echo-servers/libhv_echo.c    -Iinclude -Llib -lhv
	$(CXX) -g -Wall -std=c++11 -o bin/asio_echo     echo-servers/asio_echo.cpp   -lboost_system
	$(CXX) -g -Wall -std=c++11 -o bin/poco_echo     echo-servers/poco_echo.cpp   -lPocoNet -lPocoUtil -lPocoFoundation
	$(CXX) -g -Wall -std=c++11 -o bin/muduo_echo    echo-servers/muduo_echo.cpp  -lmuduo_net -lmuduo_base -lpthread

.PHONY: clean prepare libhv install examples test timer loop tcp udp nc nmap httpd curl consul_cli unittest webbench echo-servers
