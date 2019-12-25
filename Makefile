MAKEF=$(MAKE) -f Makefile.in
TMPDIR=tmp

default: all

all: libhv test timer loop tcp udp nc nmap httpd curl

clean:
	$(MAKEF) clean SRCDIRS=". base utils event http http/client http/server protocol examples $(TMPDIR)"

prepare:
	-mkdir -p $(TMPDIR) lib bin
	-rm base/RAII.o

libhv: prepare
	$(MAKEF) TARGET=$@ TARGET_TYPE=STATIC SRCDIRS=". base utils event http http/client http/server protocol"

test: prepare
	-rm $(TMPDIR)/*.o $(TMPDIR)/*.h $(TMPDIR)/*.c $(TMPDIR)/*.cpp
	cp main.cpp.tmpl $(TMPDIR)/main.cpp
	$(MAKEF) TARGET=$@ SRCDIRS=". base utils $(TMPDIR)"

timer: prepare
	-rm $(TMPDIR)/*.o $(TMPDIR)/*.h $(TMPDIR)/*.c $(TMPDIR)/*.cpp
	cp examples/timer.c $(TMPDIR)
	$(MAKEF) TARGET=$@ SRCDIRS=". base event $(TMPDIR)"

loop: prepare
	-rm $(TMPDIR)/*.o $(TMPDIR)/*.h $(TMPDIR)/*.c $(TMPDIR)/*.cpp
	cp examples/loop.c $(TMPDIR)
	$(MAKEF) TARGET=$@ SRCDIRS=". base event $(TMPDIR)"

tcp: prepare
	-rm $(TMPDIR)/*.o $(TMPDIR)/*.h $(TMPDIR)/*.c $(TMPDIR)/*.cpp
	cp examples/tcp.c $(TMPDIR)
	$(MAKEF) TARGET=$@ SRCDIRS=". base event $(TMPDIR)"

udp: prepare
	-rm $(TMPDIR)/*.o $(TMPDIR)/*.h $(TMPDIR)/*.c $(TMPDIR)/*.cpp
	cp examples/udp.c $(TMPDIR)
	$(MAKEF) TARGET=$@ SRCDIRS=". base event $(TMPDIR)"

nc: prepare
	-rm $(TMPDIR)/*.o $(TMPDIR)/*.h $(TMPDIR)/*.c $(TMPDIR)/*.cpp
	cp examples/nc.c $(TMPDIR)
	$(MAKEF) TARGET=$@ SRCDIRS=". base event $(TMPDIR)"

nmap: prepare
	-rm $(TMPDIR)/*.o $(TMPDIR)/*.h $(TMPDIR)/*.c $(TMPDIR)/*.cpp
	cp examples/nmap.cpp $(TMPDIR)
ifeq ($(OS), Windows)
	# for nmap on Windows platform, recommand EVENT_POLL, not EVENT_IOCP
	$(MAKEF) TARGET=$@ SRCDIRS=". base event $(TMPDIR)" DEFINES="$(DEFINES) PRINT_DEBUG EVENT_POLL"
else
	$(MAKEF) TARGET=$@ SRCDIRS=". base event $(TMPDIR)" DEFINES="$(DEFINES) PRINT_DEBUG"
endif

httpd: prepare
	-rm $(TMPDIR)/*.o $(TMPDIR)/*.h $(TMPDIR)/*.c $(TMPDIR)/*.cpp
	cp examples/httpd.cpp examples/http_api_test.h $(TMPDIR)
	$(MAKEF) TARGET=$@ SRCDIRS=". base utils event http http/server $(TMPDIR)"

curl: prepare
	-rm $(TMPDIR)/*.o $(TMPDIR)/*.h $(TMPDIR)/*.c $(TMPDIR)/*.cpp
	cp examples/curl.cpp $(TMPDIR)
	$(MAKEF) TARGET=$@ SRCDIRS="$(CURL_SRCDIRS)" SRCDIRS=". base utils http http/client $(TMPDIR)"
	#$(MAKEF) TARGET=$@ SRCDIRS="$(CURL_SRCDIRS)" SRCDIRS=". base utils http http/client $(TMPDIR)" DEFINES="$(DEFINES) WITH_CURL CURL_STATICLIB"

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

INSTALL_HEADERS=hv.h\
				\
				hconfig.h\
				base/hplatform.h\
				\
				base/hdef.h\
				base/hversion.h\
				base/hbase.h\
				base/hsysinfo.h\
				base/hproc.h\
				base/hmath.h\
				base/htime.h\
				base/herr.h\
				base/hlog.h\
				base/hmutex.h\
				base/hthread.h\
				base/hsocket.h\
				base/hbuf.h\
				base/hurl.h\
				base/hgui.h\
				base/ssl_ctx.h\
				\
				base/hstring.h\
				base/hvar.h\
				base/hobj.h\
				base/hfile.h\
				base/hdir.h\
				base/hscope.h\
				base/hthreadpool.h\
				base/hobjectpool.h\
				\
				utils/base64.h\
				utils/md5.h\
				utils/json.hpp\
				utils/singleton.h\
				utils/ifconfig.h\
				utils/iniparser.h\
				utils/hendian.h\
				utils/hmain.h\
				\
				event/hloop.h\
				event/nlog.h\
				event/nmap.h\
				\
				http/httpdef.h\
				http/http2def.h\
				http/grpcdef.h\
				http/http_content.h\
				http/HttpMessage.h\
				http/client/http_client.h\
				http/server/HttpService.h\
				http/server/HttpServer.h\
				\
				protocol/icmp.h\
				protocol/dns.h\
				protocol/ftp.h\
				protocol/smtp.h
install:
	-mkdir include
	-cp $(INSTALL_HEADERS) include/

# UNIX only
webbench: prepare
	$(CC) -o bin/webbench unittest/webbench.c

.PHONY: clean prepare libhv test timer loop tcp udp nc nmap httpd curl unittest webbench install
