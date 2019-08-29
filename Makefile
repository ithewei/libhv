MAKEF=$(MAKE) -f Makefile.in
TMPDIR=tmp

default: all

all: libhw test timer loop tcp udp nc nmap httpd curl

clean:
	$(MAKEF) clean SRCDIRS=". base utils event http http/client http/server examples $(TMPDIR)"

prepare:
	-mkdir -p $(TMPDIR)
	-rm base/RAII.o

libhw:
	$(MAKEF) TARGET=$@ TARGET_TYPE=STATIC SRCDIRS=". base utils event http http/client http/server"

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
	# for nmap, on Windows platform, we suggest EVENT_POLL, not EVENT_IOCP
	$(MAKEF) TARGET=$@ SRCDIRS=". base event $(TMPDIR)" DEFINES="$(DEFINES) PRINT_DEBUG EVENT_POLL"
else
	$(MAKEF) TARGET=$@ SRCDIRS=". base event $(TMPDIR)" DEFINES="$(DEFINES) PRINT_DEBUG"
endif

httpd: prepare
	-rm $(TMPDIR)/*.o $(TMPDIR)/*.h $(TMPDIR)/*.c $(TMPDIR)/*.cpp
	cp examples/httpd.cpp examples/http_api_test.h $(TMPDIR)
	$(MAKEF) TARGET=$@ SRCDIRS=". base utils event http http/server $(TMPDIR)"

curl:
	-rm $(TMPDIR)/*.o $(TMPDIR)/*.h $(TMPDIR)/*.c $(TMPDIR)/*.cpp
	cp examples/curl.cpp $(TMPDIR)
	$(MAKEF) TARGET=$@ SRCDIRS="$(CURL_SRCDIRS)" SRCDIRS=". base utils http http/client $(TMPDIR)"
	#$(MAKEF) TARGET=$@ SRCDIRS="$(CURL_SRCDIRS)" SRCDIRS=". base utils http http/client $(TMPDIR)" DEFINES="$(DEFINES) WITH_CURL CURL_STATICLIB"

unittest:
	$(CC)  -std=c99   -I. -Ibase         -o bin/ping       unittest/ping_test.c          base/hsocket.c base/htime.c -DPRINT_DEBUG
	$(CC)  -std=c99   -I. -Ibase         -o bin/connect    unittest/connect_test.c       base/hsocket.c base/htime.c
	$(CXX) -std=c++11 -I. -Ibase         -o bin/defer      unittest/defer_test.cpp
	$(CXX) -std=c++11 -I. -Ibase         -o bin/threadpool unittest/threadpool_test.cpp  -pthread
	$(CXX) -std=c++11 -I. -Ibase         -o bin/ls         unittest/listdir_test.cpp     base/hdir.cpp base/hbase.c
	$(CXX) -std=c++11 -I. -Ibase -Iutils -o bin/ifconfig   unittest/ifconfig_test.cpp    utils/ifconfig.cpp

# UNIX only
webbench:
	$(CC) -o bin/webbench unittest/webbench.c

.PHONY: clean prepare libhw test timer loop tcp udp nc nmap httpd curl unittest webbench
