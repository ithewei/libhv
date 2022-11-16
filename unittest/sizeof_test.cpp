#include <stdio.h>

#include "hloop.h"
#include "hevent.h"

#include "EventLoop.h"
#include "EventLoopThread.h"
#include "EventLoopThreadPool.h"
#include "Channel.h"
#include "TcpClient.h"
#include "TcpServer.h"
#include "UdpClient.h"
#include "UdpServer.h"

#include "HttpMessage.h"
#include "Http1Parser.h"
#include "HttpContext.h"
#include "HttpServer.h"
#include "HttpHandler.h"
#include "HttpResponseWriter.h"

#include "WebSocketChannel.h"
#include "WebSocketParser.h"
#include "WebSocketServer.h"
#include "WebSocketClient.h"

using namespace hv;

int main() {
    // event
    printf("sizeof(struct hloop_s)=%lu\n", sizeof(struct hloop_s));
    printf("sizeof(struct hevent_s)=%lu\n", sizeof(struct hevent_s));
    printf("sizeof(struct hidle_s)=%lu\n", sizeof(struct hidle_s));
    printf("sizeof(struct htimer_s)=%lu\n", sizeof(struct htimer_s));
    printf("sizeof(struct htimeout_s)=%lu\n", sizeof(struct htimeout_s));
    printf("sizeof(struct hperiod_s)=%lu\n", sizeof(struct hperiod_s));
    printf("sizeof(struct hio_s)=%lu\n", sizeof(struct hio_s));
    // evpp
    printf("sizeof(class EventLoop)=%lu\n", sizeof(EventLoop));
    printf("sizeof(class EventLoopThread)=%lu\n", sizeof(EventLoopThread));
    printf("sizeof(class EventLoopThreadPool)=%lu\n", sizeof(EventLoopThreadPool));
    printf("sizeof(class Channel)=%lu\n", sizeof(Channel));
    printf("sizeof(class SocketChannel)=%lu\n", sizeof(SocketChannel));
    printf("sizeof(class TcpClient)=%lu\n", sizeof(TcpClient));
    printf("sizeof(class TcpServer)=%lu\n", sizeof(TcpServer));
    printf("sizeof(class UdpClient)=%lu\n", sizeof(UdpClient));
    printf("sizeof(class UdpServer)=%lu\n", sizeof(UdpServer));
    // http
    printf("sizeof(class HttpRequest)=%lu\n", sizeof(HttpRequest));
    printf("sizeof(class HttpResponse)=%lu\n", sizeof(HttpResponse));
    printf("sizeof(class Http1Parser)=%lu\n", sizeof(Http1Parser));
    printf("sizeof(class HttpContext)=%lu\n", sizeof(HttpContext));
    printf("sizeof(class HttpServer)=%lu\n", sizeof(HttpServer));
    printf("sizeof(class HttpHandler)=%lu\n", sizeof(HttpHandler));
    printf("sizeof(class HttpResponseWrite)=%lu\n", sizeof(HttpResponseWriter));
    // websocket
    printf("sizeof(class WebSocketChannel)=%lu\n", sizeof(WebSocketChannel));
    printf("sizeof(class WebSocketParser)=%lu\n", sizeof(WebSocketParser));
    printf("sizeof(class WebSocketClient)=%lu\n", sizeof(WebSocketClient));
    printf("sizeof(class WebSocketServer)=%lu\n", sizeof(WebSocketServer));
    return 0;
}
