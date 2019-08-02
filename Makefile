MAKEF=$(MAKE) -f Makefile.in
TMPDIR=tmp

default: all

all: test ping loop client server httpd

clean:
	$(MAKEF) clean SRCDIRS=". base utils event http http/client http/server examples $(TMPDIR)"

prepare:
	-mkdir -p $(TMPDIR)

test: prepare
	-rm $(TMPDIR)/*.o $(TMPDIR)/*.h $(TMPDIR)/*.c $(TMPDIR)/*.cpp
	cp main.cpp.tmpl $(TMPDIR)/main.cpp
	$(MAKEF) TARGET=$@ SRCDIRS=". base utils $(TMPDIR)"

ping:
	-rm base/hsocket.o
	$(MAKEF) TARGET=$@ SRCDIRS="" SRCS="examples/ping.c base/hsocket.c base/htime.c base/RAII.cpp" INCDIRS="base" DEFINES="PRINT_DEBUG"

loop: prepare
	-rm $(TMPDIR)/*.o $(TMPDIR)/*.h $(TMPDIR)/*.c $(TMPDIR)/*.cpp
	cp examples/loop.c $(TMPDIR)/loop.c
	$(MAKEF) TARGET=$@ SRCDIRS=". base event $(TMPDIR)"

client: prepare
	-rm $(TMPDIR)/*.o $(TMPDIR)/*.c $(TMPDIR)/*.cpp
	cp examples/client.cpp $(TMPDIR)/client.cpp
	$(MAKEF) TARGET=$@ SRCDIRS=". base event $(TMPDIR)"

server: prepare
	-rm $(TMPDIR)/*.o $(TMPDIR)/*.c $(TMPDIR)/*.cpp
	cp examples/server.cpp $(TMPDIR)/server.cpp
	$(MAKEF) TARGET=$@ SRCDIRS=". base event $(TMPDIR)"

httpd: prepare
	-rm $(TMPDIR)/*.o $(TMPDIR)/*.c $(TMPDIR)/*.cpp
	cp examples/httpd.cpp $(TMPDIR)/httpd.cpp
	cp examples/httpd_conf.h $(TMPDIR)/httpd_conf.h
	cp examples/http_api_test.h $(TMPDIR)/http_api_test.h
	$(MAKEF) TARGET=$@ SRCDIRS=". base utils event http http/server $(TMPDIR)"

webbench:
	$(MAKEF) TARGET=$@ SRCDIRS="" SRCS="examples/webbench.c"

# curl
CURL_SRCDIRS := http http/client
CURL_INCDIRS += base utils http http/client
CURL_SRCS    += examples/curl.cpp base/hstring.cpp
curl:
	$(MAKEF) TARGET=$@ SRCDIRS="$(CURL_SRCDIRS)" INCDIRS="$(CURL_INCDIRS)" SRCS="$(CURL_SRCS)" DEFINES="CURL_STATICLIB" LIBS="curl"

.PHONY: clean prepare test ping loop client server httpd webbench curl
