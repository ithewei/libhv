#include "hv/hmain.h"
#include "hv/hloop.h"
#include "hv/hsocket.h"

#include "hv/EventLoopThreadPool.h"
using namespace hv;

static const char options[] = "hH:p:t:c:s:b:";

static const char detail_options[] = R"(
  -h                Print help
  -H <Host>         default 127.0.0.1
  -p <port>
  -t <threads>      default 4
  -c <connections>  default 1000
  -s <seconds>      default 10
  -b <bytes>        send buffer size, default 1024
)";

static const char* host = "127.0.0.1";
static int port = 0;
static int threads = 4;
static int connections = 1000;
static int seconds = 10;
static int sendbytes = 1024;
static void* sendbuf = NULL;

static std::atomic<int> connected_num(0);
static std::atomic<int> disconnected_num(0);
static std::atomic<uint64_t> total_readcount(0);
static std::atomic<uint64_t> total_readbytes(0);

static void print_help() {
    printf("Usage: %s [%s]\n", g_main_ctx.program_name, options);
    printf("Options:\n%s\n", detail_options);
}

static void print_result() {
    printf("total readcount=%llu readbytes=%llu\n",
        (unsigned long long)total_readcount,
        (unsigned long long)total_readbytes);
    printf("throughput = %llu MB/s\n", (total_readbytes) / ((unsigned long long)seconds * 1024 * 1024));
}

static void on_close(hio_t* io) {
    if (++disconnected_num == connections) {
        printf("all disconnected\n");
    }
}

void on_recv(hio_t* io, void* buf, int readbytes) {
    ++total_readcount;
    total_readbytes += readbytes;
    hio_write(io, buf, readbytes);
}

static void on_connect(hio_t* io) {
    if (++connected_num == connections) {
        printf("all connected\n");
    }

    tcp_nodelay(hio_fd(io), 1);
    hio_setcb_read(io, on_recv);
    hio_setcb_close(io, on_close);
    hio_read_start(io);

    hio_write(io, sendbuf, sendbytes);
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
    const char* strThreads = get_arg("t");
    const char* strConnections = get_arg("c");
    const char* strSeconds = get_arg("s");
    const char* strBytes = get_arg("b");

    if (strHost)        host = strHost;
    if (strPort)        port = atoi(strPort);
    if (strThreads)     threads = atoi(strThreads);
    if (strConnections) connections = atoi(strConnections);
    if (strSeconds)     seconds = atoi(strSeconds);
    if (strBytes)       sendbytes = atoi(strBytes);

    if (get_arg("h") || port == 0) {
        print_help();
        exit(0);
    }
    sendbuf = malloc(sendbytes);

    printf("[%s:%d] %d threads %d connections run %ds\n",
        host, port,
        threads, connections, seconds);

    EventLoopThreadPool loop_threads(threads);
    loop_threads.start(true);
    for (int i = 0; i < connections; ++i) {
        EventLoopPtr loop = loop_threads.nextLoop();
        loop->runInLoop(std::bind(hloop_create_tcp_client, loop->loop(), host, port, on_connect));
    }

    // stop after seconds
    loop_threads.loop()->setTimeout(seconds * 1000, [&loop_threads](TimerID timerID){
        loop_threads.stop(false);
    });

    // wait loop_threads exit
    loop_threads.join();

    print_result();

    return 0;
}
