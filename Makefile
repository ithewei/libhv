MAKEF=$(MAKE) -f Makefile.in
TMPDIR=tmp

default: all

all: libhw test ping timer loop tcp udp nc nmap httpd

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

ping:
	-rm base/hsocket.o
	$(MAKEF) TARGET=$@ SRCDIRS="" SRCS="examples/ping.c base/hsocket.c base/htime.c base/RAII.cpp" INCDIRS=". base" DEFINES="$(DEFINES) PRINT_DEBUG"

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

webbench:
	$(MAKEF) TARGET=$@ SRCDIRS="" SRCS="examples/webbench.c"

curl:
	-rm $(TMPDIR)/*.o $(TMPDIR)/*.h $(TMPDIR)/*.c $(TMPDIR)/*.cpp
	cp examples/curl.cpp $(TMPDIR)
	$(MAKEF) TARGET=$@ SRCDIRS="$(CURL_SRCDIRS)" SRCDIRS=". base utils http http/client $(TMPDIR)"
	#$(MAKEF) TARGET=$@ SRCDIRS="$(CURL_SRCDIRS)" SRCDIRS=". base utils http http/client $(TMPDIR)" DEFINES="$(DEFINES) WITH_CURL CURL_STATICLIB"

.PHONY: clean prepare libhw test ping timer loop tcp udp nc nmap httpd webbench curl
