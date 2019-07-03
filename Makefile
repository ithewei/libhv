reset:
	-mkdir -p test
	-rm test/*.c test/*.cpp test/*.o;

clean:
	make clean SRCDIRS=". base utils event http test" -f Makefile.in

test: reset
	cp main.cpp.tmpl test/main.cpp
	make TARGET="test" SRCDIRS=". base utils test" -f Makefile.in

client: reset
	cp event/client.cpp.demo test/client.cpp
	make TARGET="client" SRCDIRS=". base event test" -f Makefile.in

server: reset
	cp event/server.cpp.demo test/server.cpp
	make TARGET="server" SRCDIRS=". base event test" -f Makefile.in

curl: reset
	cp http/curl.cpp.demo test/curl.cpp
	cp http/http_client.cpp.curl test/http_client.cpp
	make TARGET="curl" SRCDIRS=". base utils event http test" LIBS="curl" -f Makefile.in

httpd: reset
	cp http/httpd.cpp.demo test/httpd.cpp
	make TARGET="httpd" SRCDIRS=". base utils event http test" -f Makefile.in

webbench: reset
	cp http/webbench.c.demo test/webbench.c
	make TARGET="webbench" SRCDIRS="test" -f Makefile.in

.PHONY: reset clean test client server curl httpd
