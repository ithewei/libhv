/*
 * @build: make examples
 * @server bin/httpd -s restart -d
 * @client bin/curl -v http://127.0.0.1:8080/
 * @usage: bin/wrk -c 1000 -d 10 -t 4 http://127.0.0.1:8080/
 *
 */

#include "hv.h"
#include "hmain.h"  // import parse_opt
#include "hloop.h"

#include "EventLoopThreadPool.h"
#include "HttpMessage.h"
#include "HttpParser.h"
using namespace hv;

static const char options[] = "hvc:d:t:";

static const char detail_options[] = R"(
  -h                Print help infomation
  -v                Show verbose infomation
  -c <connections>  Number of connections, default: 1000
  -d <duration>     Duration of test, default: 10s
  -t <threads>      Number of threads, default: 4
)";

static int connections = 1000;
static int duration = 10;
static int threads = 4;

static bool verbose = false;
static const char* url = NULL;
static bool https = false;
static char ip[64] = "127.0.0.1";
static int  port = 80;

static bool stop = false;

static HttpRequestPtr   request;
static std::string      request_msg;

typedef struct connection_s {
    hio_t*          io;
    HttpParserPtr   parser;
    HttpResponsePtr response;
    uint64_t request_cnt;
    uint64_t response_cnt;
    uint64_t ok_cnt;
    uint64_t readbytes;

    connection_s()
        : parser(HttpParser::New(HTTP_CLIENT, HTTP_V1))
        , response(new HttpResponse)
        , request_cnt(0)
        , response_cnt(0)
        , ok_cnt(0)
        , readbytes(0)
    {
        response->http_cb = [](HttpMessage* res, http_parser_state state, const char* data, size_t size) {
            // wrk no need to save data to body
        };
    }

    void SendRequest() {
        hio_write(io, request_msg.data(), request_msg.size());
        ++request_cnt;
        parser->InitResponse(response.get());
    }

    bool RecvResponse(const char* data, int size) {
        readbytes += size;
        int nparse = parser->FeedRecvData(data, size);
        if (nparse != size) {
            fprintf(stderr, "http parse error!\n");
            hio_close(io);
            return false;
        }
        if (parser->IsComplete()) {
            ++response_cnt;
            if (response->status_code == HTTP_STATUS_OK) {
                ++ok_cnt;
            }
            return true;
        }
        return false;
    }
} connection_t;
static connection_t** conns = NULL;

static std::atomic<int> connected_num(0);
static std::atomic<int> disconnected_num(0);

static void print_help() {
    printf("Usage: wrk [%s] <url>\n", options);
    printf("Options:\n%s\n", detail_options);
}

static void print_cmd() {
    printf("Running %ds test @ %s\n", duration, url);
    printf("%d threads and %d connections\n", threads, connections);
}

static void print_result() {
    uint64_t total_request_cnt = 0;
    uint64_t total_response_cnt = 0;
    uint64_t total_ok_cnt = 0;
    uint64_t total_readbytes = 0;
    connection_t* conn = NULL;
    for (int i = 0; i < connections; ++i) {
        conn = conns[i];
        total_request_cnt += conn->request_cnt;
        total_response_cnt += conn->response_cnt;
        total_ok_cnt += conn->ok_cnt;
        total_readbytes += conn->readbytes;
    }
    printf("%llu requests, %llu OK, %lluMB read in %ds\n",
            LLU(total_request_cnt),
            LLU(total_ok_cnt),
            LLU(total_readbytes >> 20),
            duration);
    printf("Requests/sec: %8llu\n", LLU(total_response_cnt / duration));
    printf("Transfer/sec: %8lluMB\n", LLU((total_readbytes / duration) >> 20));
}

static void start_reconnect(hio_t* io);
static void on_close(hio_t* io) {
    if (++disconnected_num == connections) {
        if (verbose) {
            printf("all disconnected\n");
        }
    }

    if (!stop) {
        // NOTE: nginx keepalive_requests = 100
        start_reconnect(io);
    }
}

static void on_recv(hio_t* io, void* buf, int readbytes) {
    connection_t* conn = (connection_t*)hevent_userdata(io);
    if (conn->RecvResponse((const char*)buf, readbytes)) {
        conn->SendRequest();
    }
}

static void on_connect(hio_t* io) {
    if (++connected_num == connections) {
        if (verbose) {
            printf("all connected\n");
        }
    }

    connection_t* conn = (connection_t*)hevent_userdata(io);
    conn->SendRequest();

    hio_setcb_read(io, on_recv);
    hio_read(io);
}

static void start_connect(hloop_t* loop, connection_t* conn) {
    hio_t* io = hio_create_socket(loop, ip, port, HIO_TYPE_TCP, HIO_CLIENT_SIDE);
    if (io == NULL) {
        perror("socket");
        exit(1);
    }
    conn->io = io;
    hevent_set_userdata(io, conn);
    if (https) {
        hio_enable_ssl(io);
    }
    tcp_nodelay(hio_fd(io), 1);
    hio_setcb_connect(io, on_connect);
    hio_setcb_close(io, on_close);
    hio_connect(io);
}

static void start_reconnect(hio_t* io) {
    hloop_t* loop = hevent_loop(io);
    connection_t* conn = (connection_t*)hevent_userdata(io);
    start_connect(loop, conn);
}

int main(int argc, char** argv) {
    // parse cmdline
    main_ctx_init(argc, argv);
    int ret = parse_opt(argc, argv, options);
    if (ret != 0) {
        print_help();
        exit(ret);
    }

    if (get_arg("h") || g_main_ctx.arg_list_size != 1) {
        print_help();
        exit(0);
    }
    url = g_main_ctx.arg_list[0];

    if (get_arg("v")) {
        verbose = true;
    }

    const char* strConnections = get_arg("c");
    const char* strDuration = get_arg("d");
    const char* strThreads = get_arg("t");

    if (strConnections) connections = atoi(strConnections);
    if (strDuration)    duration = atoi(strDuration);
    if (strThreads)     threads = atoi(strThreads);

    print_cmd();

    // ParseUrl
    request.reset(new HttpRequest);
    request->url = url;
    request->ParseUrl();
    https = request->scheme == "https";
    const char* host = request->host.c_str();
    port = request->port;

    // ResolveAddr
    if (is_ipaddr(host)) {
        strcpy(ip, host);
    } else {
        sockaddr_u addr;
        if (ResolveAddr(host, &addr) != 0) {
            fprintf(stderr, "Could not resolve host: %s\n", host);
            exit(1);
        }
        sockaddr_ip(&addr, ip, sizeof(ip));
    }

    // Test connect
    printf("Connect to %s:%d ...\n", ip, port);
    int connfd = ConnectTimeout(ip, port);
    if (connfd < 0) {
        fprintf(stderr, "Could not connect to %s:%d\n", ip, port);
        exit(1);
    } else {
        closesocket(connfd);
    }

    // Dump request
    request->headers["User-Agent"] = std::string("libhv/") + hv_version();
    request->headers["Connection"] = "keep-alive";
    request_msg = request->Dump(true, true);
    printf("%s", request_msg.c_str());

    // EventLoopThreadPool
    EventLoopThreadPool loop_threads(threads);
    loop_threads.start(true);

    // connections
    conns = (connection_t**)malloc(sizeof(connection_t*) * connections);
    for (int i = 0; i < connections; ++i) {
        conns[i] = new connection_t;

        EventLoopPtr loop = loop_threads.nextLoop();
        hloop_t* hloop = loop->loop();
        loop->runInLoop(std::bind(start_connect, loop->loop(), conns[i]));
    }

    // stop after duration
    loop_threads.loop()->setTimeout(duration * 1000, [&loop_threads](TimerID timerID){
        stop = true;
        loop_threads.stop(false);
    });

    // wait loop_threads exit
    loop_threads.join();

    print_result();

    return 0;
}
