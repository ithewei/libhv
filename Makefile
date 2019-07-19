MAKEF=$(MAKE) -f Makefile.in
TMPDIR=tmp

default: all

all: test client server httpd webbench

clean:
	$(MAKEF) clean SRCDIRS=". base utils event http http/client http/server examples $(TMPDIR)"

prepare:
	-mkdir -p $(TMPDIR)

test: prepare
	-rm $(TMPDIR)/*.o $(TMPDIR)/*.h $(TMPDIR)/*.c $(TMPDIR)/*.cpp
	cp main.cpp.tmpl $(TMPDIR)/main.cpp
	$(MAKEF) TARGET=$@ SRCDIRS=". base utils $(TMPDIR)"

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

webbench: prepare
	$(MAKEF) TARGET=$@ SRCS="examples/webbench.c"

# curl
INCDIRS:=". base utils http http/client"
SRCS:="examples/curl.cpp http/client/http_client.cpp http/http_parser.c http/multipart_parser.c http/http_content.cpp base/hstring.cpp"
curl:
	$(MAKEF) TARGET=$@ INCDIRS=$(INCDIRS) SRCS=$(SRCS) DEFINES="CURL_STATICLIB" LIBS="curl"

.PHONY: clean prepare test client server curl httpd webbench
