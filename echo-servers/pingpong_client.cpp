#include "hv/hmain.h" // import parse_opt
#include "hv/hloop.h"
#include "hv/hsocket.h"

#include "hv/EventLoopThreadPool.h"
using namespace hv;

static const char options[] = "hvH:p:c:d:t:b:";

static const char detail_options[] = R"(
  -h                Print help infomation
  -v                Show verbose infomation
  -H <Host>         default 127.0.0.1
  -p <port>
  -c <connections>  Number of connections, default: 1000
  -d <duration>     Duration of test, default: 10s
  -t <threads>      Number of threads, default: 4
  -b <bytes>        Bytes of send buffer, default: 4096
)";

static int connections = 1000;
static int duration = 10;
static int threads = 4;

static bool verbose = false;
static const char* host = "127.0.0.1";
static int port = 0;
static int sendbytes = 4096;
static void* sendbuf = NULL;

static std::atomic<int> connected_num(0);
static std::atomic<int> disconnected_num(0);
static std::atomic<uint64_t> total_readcount(0);
static std::atomic<uint64_t> total_readbytes(0);

static void print_help() {
    printf("Usage: %s [%s]\n", g_main_ctx.program_name, options);
    printf("Options:\n%s\n", detail_options);
}

static void print_cmd() {
    printf("Running %ds test @ %s:%d\n", duration, host, port);
    printf("%d threads and %d connections, send %d bytes each time\n", threads, connections, sendbytes);
}

static void print_result() {
    printf("total readcount=%llu readbytes=%llu\n",
        (unsigned long long)total_readcount,
        (unsigned long long)total_readbytes);
    printf("throughput = %llu MB/s\n", (total_readbytes) / ((unsigned long long)duration * 1024 * 1024));
}

static void on_close(hio_t* io) {
    if (++disconnected_num == connections) {
        if (verbose) {
            printf("all disconnected\n");
        }
    }
}

static void on_recv(hio_t* io, void* buf, int readbytes) {
    ++total_readcount;
    total_readbytes += readbytes;
    hio_write(io, buf, readbytes);
}

static void on_connect(hio_t* io) {
    if (++connected_num == connections) {
        if (verbose) {
            printf("all connected\n");
        }
    }

    hio_write(io, sendbuf, sendbytes);

    hio_setcb_read(io, on_recv);
    hio_read_start(io);
}

static void start_connect(hloop_t* loop) {
    hio_t* io = hio_create_socket(loop, host, port, HIO_TYPE_TCP, HIO_CLIENT_SIDE);
    if (io == NULL) {
        perror("socket");
        exit(1);
    }
    tcp_nodelay(hio_fd(io), 1);
    hio_setcb_connect(io, on_connect);
    hio_setcb_close(io, on_close);
    hio_connect(io);
}

int main(int argc, char** argv) {
    // parse cmdline
    main_ctx_init(argc, argv);
    int ret = parse_opt(argc, argv, options);
    if (ret != 0) {
        print_help();
        exit(ret);
    }
    const char* strHost = get_arg("H");
    const char* strPort = get_arg("p");
    const char* strConnections = get_arg("c");
    const char* strDuration = get_arg("d");
    const char* strThreads = get_arg("t");
    const char* strBytes = get_arg("b");

    if (strHost)        host = strHost;
    if (strPort)        port = atoi(strPort);
    if (strConnections) connections = atoi(strConnections);
    if (strDuration)    duration = atoi(strDuration);
    if (strThreads)     threads = atoi(strThreads);
    if (strBytes)       sendbytes = atoi(strBytes);

    if (get_arg("h") || port == 0) {
        print_help();
        exit(0);
    }
    if (get_arg("v")) {
        verbose = true;
    }
    sendbuf = malloc(sendbytes);

    print_cmd();

    EventLoopThreadPool loop_threads(threads);
    loop_threads.start(true);
    for (int i = 0; i < connections; ++i) {
        EventLoopPtr loop = loop_threads.nextLoop();
        loop->runInLoop(std::bind(start_connect, loop->loop()));
    }

    // stop after seconds
    loop_threads.loop()->setTimeout(duration * 1000, [&loop_threads](TimerID timerID){
        loop_threads.stop(false);
    });

    // wait loop_threads exit
    loop_threads.join();

    print_result();

    return 0;
}
