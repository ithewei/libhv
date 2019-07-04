MAKEF=$(MAKE) -f Makefile.in
TMPDIR=tmp

default: all

all: test client server httpd webbench

clean:
	$(MAKEF) clean SRCDIRS=". base utils event http $(TMPDIR)"

prepare:
	-mkdir -p $(TMPDIR)

test: prepare
	-rm $(TMPDIR)/*.o $(TMPDIR)/*.c $(TMPDIR)/*.cpp
	cp main.cpp.tmpl $(TMPDIR)/main.cpp
	$(MAKEF) TARGET=$@ SRCDIRS=". base utils $(TMPDIR)"

client: prepare
	-rm $(TMPDIR)/*.o $(TMPDIR)/*.c $(TMPDIR)/*.cpp
	cp event/client.cpp.demo $(TMPDIR)/client.cpp
	$(MAKEF) TARGET=$@ SRCDIRS=". base event $(TMPDIR)"

server: prepare
	-rm $(TMPDIR)/*.o $(TMPDIR)/*.c $(TMPDIR)/*.cpp
	cp event/server.cpp.demo $(TMPDIR)/server.cpp
	$(MAKEF) TARGET=$@ SRCDIRS=". base event $(TMPDIR)"

httpd: prepare
	-rm $(TMPDIR)/*.o $(TMPDIR)/*.c $(TMPDIR)/*.cpp
	cp http/httpd.cpp.demo $(TMPDIR)/httpd.cpp
	$(MAKEF) TARGET=$@ SRCDIRS=". base utils event http $(TMPDIR)"

webbench: prepare
	-rm $(TMPDIR)/*.o $(TMPDIR)/*.c $(TMPDIR)/*.cpp
	cp http/webbench.c.demo $(TMPDIR)/webbench.c
	$(MAKEF) TARGET=$@ SRCS="$(TMPDIR)/webbench.c"

curl: prepare
	-rm $(TMPDIR)/*.o $(TMPDIR)/*.c $(TMPDIR)/*.cpp
	cp http/curl.cpp.demo $(TMPDIR)/curl.cpp
	cp http/http_client.cpp.curl $(TMPDIR)/http_client.cpp
	$(MAKEF) TARGET=$@ SRCDIRS=". base utils event http $(TMPDIR)" LIBS="curl"

.PHONY: clean prepare test client server curl httpd webbench
