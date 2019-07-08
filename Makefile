MAKEF=$(MAKE) -f Makefile.in
TMPDIR=tmp

default: all

all: test client server httpd webbench

clean:
	$(MAKEF) clean SRCDIRS=". base utils event http $(TMPDIR)"

prepare:
	-mkdir -p $(TMPDIR)

test: prepare
	-rm $(TMPDIR)/*.o $(TMPDIR)/*.h $(TMPDIR)/*.c $(TMPDIR)/*.cpp
	cp main.cpp.tmpl $(TMPDIR)/main.cpp
	$(MAKEF) TARGET=$@ SRCDIRS=". base utils $(TMPDIR)"

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
	-rm $(TMPDIR)/*.o $(TMPDIR)/*.c $(TMPDIR)/*.cpp
	cp examples/webbench.c $(TMPDIR)/webbench.c
	$(MAKEF) TARGET=$@ SRCS="$(TMPDIR)/webbench.c"

curl: prepare
	-rm $(TMPDIR)/*.o $(TMPDIR)/*.c $(TMPDIR)/*.cpp
	cp examples/curl.cpp $(TMPDIR)/curl.cpp
	$(MAKEF) TARGET=$@ SRCDIRS=". base utils event http http/client $(TMPDIR)" LIBS="curl"

.PHONY: clean prepare test client server curl httpd webbench
