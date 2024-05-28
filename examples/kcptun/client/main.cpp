/*
 * kcptun client
 *
 * @build:          ./configure --with-kcp && make clean && make kcptun examples
 * @tcp_server:     bin/tcp_echo_server 1234
 * @kcptun_server:  bin/kcptun_server -l :4000 -t 127.0.0.1:1234
 * @kcptun_client:  bin/kcptun_client -l :8388 -r 127.0.0.1:4000
 * @tcp_client:     bin/nc 127.0.0.1 8388
 *                  > hello
 *                  < hello
 */

#define WITH_KCP 1
#include "hversion.h"
#include "hmain.h"
#include "hsocket.h"
#include "hloop.h"
#include "hthread.h"

#include "../smux/smux.h"

// config
static const char* localaddr = ":8388";
static const char* remoteaddr = "127.0.0.1:4000";
static const char* mode = "fast";
static int mtu = 1350;
static int sndwnd = 128;
static int rcvwnd = 512;

// long options
static const option_t long_options[] = {
    {'h', "help",       NO_ARGUMENT,        "Print this information"},
    {'v', "version",    NO_ARGUMENT,        "Print version"},
    {'d', "daemon",     NO_ARGUMENT,        "Daemonize"},
    {'l', "localaddr",  REQUIRED_ARGUMENT,  "local listen address (default: \":8388\")"},
    {'r', "remoteaddr", REQUIRED_ARGUMENT,  "kcp server address (default: \"127.0.0.1:4000\")"},
    {'m', "mode",       REQUIRED_ARGUMENT,  "profiles: fast3, fast2, fast, normal, (default: \"fast\")"},
    { 0,  "mtu",        REQUIRED_ARGUMENT,  "set maxinum transmission unit for UDP packets (default: 1350)"},
    { 0,  "sndwnd",     REQUIRED_ARGUMENT,  "set send window size(num of packets) (default: 128)"},
    { 0,  "rcvwnd",     REQUIRED_ARGUMENT,  "set receive window size(num of packets) (default: 512)"},
};

static void print_version() {
    printf("%s version %s\n", g_main_ctx.program_name, hv_compile_version());
}

static void print_help() {
    char detail_options[1024] = {0};
    dump_opt_long(long_options, ARRAY_SIZE(long_options), detail_options, sizeof(detail_options));
    printf("%s\n", detail_options);
}

static char listen_host[64] = "0.0.0.0";
static int  listen_port = 8388;
static hio_t* listen_io = NULL;

static kcp_setting_t s_kcp_setting;
static char kcp_host[64] = "127.0.0.1";
static int  kcp_port = 4000;
static hio_t* kcp_io = NULL;

static smux_config_t  smux_config;
static smux_session_t smux_session;
static smux_stream_t  smux_stream0;

static int verbose = 1;

typedef struct kcp_ctx_s {
    smux_frame_t    frame;
    uint16_t        want_readbytes;
} kcp_ctx_t;

/* workflow:
 *
 * hloop_create_tcp_server ->
 * on_accept -> smux_session_open_stream ->
 * on_recv -> hio_write(kcp_io) ->
 * on_close -> smux_session_close_stream
 *
 * hloop_create_udp_client -> hio_set_kcp ->
 * on_recvfrom -> smux_session_get_stream -> hio_write(stream_io)
 *
 */

static void send_hearbeat(htimer_t* timer) {
    smux_stream_t* smux_stream = (smux_stream_t*)hevent_userdata(timer);
    if (smux_stream == NULL) return;
    // NOP
    int packlen = smux_stream_output(smux_stream, SMUX_CMD_NOP);
    if (packlen > 0) {
        // printf("NOP %d\n", packlen);
        hio_write(kcp_io, smux_stream->wbuf.base, packlen);
    }
}

static void on_close(hio_t* io) {
    // printf("on_close fd=%d error=%d\n", hio_fd(io), hio_error(io));
    if (verbose) {
        char localaddrstr[SOCKADDR_STRLEN] = {0};
        char peeraddrstr[SOCKADDR_STRLEN] = {0};
        printf("disconnected connfd=%d [%s] <= [%s]\n", hio_fd(io),
            SOCKADDR_STR(hio_localaddr(io), localaddrstr),
            SOCKADDR_STR(hio_peeraddr(io), peeraddrstr));
    }

    smux_stream_t* smux_stream = (smux_stream_t*)hevent_userdata(io);
    if (smux_stream == NULL) return;

    // FIN
    int packlen = smux_stream_output(smux_stream, SMUX_CMD_FIN);
    if (packlen > 0) {
        // printf("FIN %d\n", packlen);
        hio_write(kcp_io, smux_stream->wbuf.base, packlen);
    }

    // kill timer
    if (smux_stream->timer) {
        htimer_del(smux_stream->timer);
        smux_stream->timer = NULL;
    }

    // free buffer
    HV_FREE(smux_stream->rbuf.base);
    HV_FREE(smux_stream->wbuf.base);

    smux_session_close_stream(&smux_session, smux_stream->stream_id);
    hevent_set_userdata(io, NULL);
}

static void on_recv(hio_t* io, void* buf, int readbytes) {
    // printf("on_recv %.*s \n", readbytes, (char*)buf);
    smux_stream_t* smux_stream = (smux_stream_t*)hevent_userdata(io);
    if (smux_stream == NULL) return;

    // PSH
    smux_frame_t frame;
    smux_frame_init(&frame);
    frame.head.sid = smux_stream->stream_id;
    frame.head.cmd = SMUX_CMD_PSH;
    frame.head.length = readbytes;
    frame.data = (const char*)buf;
    int packlen = smux_frame_pack(&frame, smux_stream->wbuf.base, smux_stream->wbuf.len);
    if (packlen > 0) {
        // printf("PSH %d\n", packlen);
        int nwrite = hio_write(kcp_io, smux_stream->wbuf.base, packlen);
        // printf("PSH ret=%d\n", nwrite);
    }
}

static void on_accept(hio_t* io) {
    // printf("on_accept connfd=%d\n", hio_fd(io));
    if (verbose) {
        char localaddrstr[SOCKADDR_STRLEN] = {0};
        char peeraddrstr[SOCKADDR_STRLEN] = {0};
        printf("accept connfd=%d [%s] <= [%s]\n", hio_fd(io),
                SOCKADDR_STR(hio_localaddr(io), localaddrstr),
                SOCKADDR_STR(hio_peeraddr(io), peeraddrstr));
    }

    hio_setcb_close(io, on_close);

    smux_stream_t* smux_stream = smux_session_open_stream(&smux_session, 0, io);
    // alloc buffer
    smux_stream->rbuf.len = mtu;
    smux_stream->wbuf.len = mtu;
    HV_ALLOC(smux_stream->rbuf.base, smux_stream->rbuf.len);
    HV_ALLOC(smux_stream->wbuf.base, smux_stream->wbuf.len);
    hio_set_readbuf(io, smux_stream->rbuf.base, smux_config.max_frame_size);
    /*
    // set heartbeat timer
    if (smux_config.keepalive_interval > 0) {
        smux_stream->timer = htimer_add(hevent_loop(io), send_hearbeat, smux_config.keepalive_interval, INFINITE);
        hevent_set_userdata(smux_stream->timer, smux_stream);
    }
    */
    hevent_set_userdata(io, smux_stream);

    // SYN
    int packlen = smux_stream_output(smux_stream, SMUX_CMD_SYN);
    if (packlen > 0) {
        // printf("SYN %d\n", packlen);
        hio_write(kcp_io, smux_stream->wbuf.base, packlen);
    }

    hio_setcb_read(io, on_recv);
    hio_read(io);
}

static void on_kcp_recvfrom(hio_t* io, void* buf, int readbytes) {
    // printf("on_kcp_recvfrom %d\n", readbytes);

    kcp_ctx_t* kcp_ctx = (kcp_ctx_t*)hevent_userdata(io);
    smux_frame_t* frame = &kcp_ctx->frame;
    if (kcp_ctx->want_readbytes > 0) {
        frame->data = (const char*)buf;
        frame->head.length = readbytes;
        kcp_ctx->want_readbytes -= readbytes;
    }
    else {
        smux_frame_init(frame);
        int packlen = smux_frame_unpack(frame, buf, readbytes);
        if (packlen < 0 ||
            frame->head.version > 2 ||
            frame->head.cmd > SMUX_CMD_UPD) {
            fprintf(stderr, "smux_frame_unpack error: %d\n", packlen);
            return;
        }
        int datalen = packlen - SMUX_HEAD_LENGTH;
        if (datalen < frame->head.length) {
            kcp_ctx->want_readbytes = frame->head.length - datalen;
            frame->head.length = datalen;
        }
    }
    // printf("smux sid=%u cmd=%d length=%d\n", frame->head.sid, (int)frame->head.cmd, (int)frame->head.length);

    smux_stream_t* smux_stream = smux_session_get_stream(&smux_session, kcp_ctx->frame.head.sid);
    if (smux_stream == NULL) {
        if (frame->head.sid != 0 && frame->head.cmd != SMUX_CMD_FIN) {
            fprintf(stderr, "recvfrom invalid smux package!\n");
        }
        return;
    }

    switch (frame->head.cmd) {
    case SMUX_CMD_SYN:
        hio_setcb_read(smux_stream->io, on_recv);
        hio_read(smux_stream->io);
        break;
    case SMUX_CMD_FIN:
        hio_close(smux_stream->io);
        break;
    case SMUX_CMD_PSH:
        hio_write(smux_stream->io, frame->data, frame->head.length);
        break;
    case SMUX_CMD_NOP:
        break;
    default:
        break;
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_help();
        exit(0);
    }

    // g_main_ctx
    main_ctx_init(argc, argv);
    int ret = parse_opt_long(argc, argv, long_options, ARRAY_SIZE(long_options));
    if (ret != 0) {
        print_help();
        exit(ret);
    }

    // help
    if (get_arg("h")) {
        print_help();
        exit(0);
    }

    // version
    if (get_arg("v")) {
        print_version();
        exit(0);
    }

#ifdef OS_UNIX
    // daemon
    if (get_arg("d")) {
        // nochdir, noclose
        int ret = daemon(1, 1);
        if (ret != 0) {
            printf("daemon error: %d\n", ret);
            exit(-10);
        }
    }
#endif

    const char* arg = get_arg("l");
    if (arg) {
        localaddr = arg;
    }

    arg = get_arg("r");
    if (arg) {
        remoteaddr = arg;
    }

    arg = get_arg("m");
    if (arg) {
        mode = arg;
    }

    arg = get_arg("mtu");
    if (arg) {
        mtu = atoi(arg);
    }

    arg = get_arg("sndwnd");
    if (arg) {
        sndwnd = atoi(arg);
    }

    arg = get_arg("rcvwnd");
    if (arg) {
        rcvwnd = atoi(arg);
    }

    const char* pos = strchr(localaddr, ':');
    int len = 0;
    if (pos) {
        len = pos - localaddr;
        if (len > 0) {
            memcpy(listen_host, localaddr, len);
            listen_host[len] = '\0';
        }
        listen_port = atoi(pos + 1);
    }

    pos = strchr(remoteaddr, ':');
    if (pos) {
        len = pos - remoteaddr;
        if (len > 0) {
            memcpy(kcp_host, remoteaddr, len);
            kcp_host[len] = '\0';
        }
        kcp_port = atoi(pos + 1);
    }

    if (strcmp(mode, "normal") == 0) {
        kcp_setting_init_with_normal_mode(&s_kcp_setting);
    } else if (strcmp(mode, "fast") == 0) {
        kcp_setting_init_with_fast_mode(&s_kcp_setting);
    } else if (strcmp(mode, "fast2") == 0) {
        kcp_setting_init_with_fast2_mode(&s_kcp_setting);
    } else if (strcmp(mode, "fast3") == 0) {
        kcp_setting_init_with_fast3_mode(&s_kcp_setting);
    } else {
        fprintf(stderr, "Unknown mode '%s'\n", mode);
        exit(-20);
    }
    s_kcp_setting.conv = hv_getpid();
    s_kcp_setting.mtu = mtu;
    s_kcp_setting.sndwnd = sndwnd;
    s_kcp_setting.rcvwnd = rcvwnd;

    printf("smux version: 1\n");
    printf("%s:%d => %s:%d\n", listen_host, listen_port, kcp_host, kcp_port);
    printf("mode: %s\n", mode);
    printf("mtu: %d\n", mtu);
    printf("sndwnd: %d rcvwnd: %d\n", sndwnd, rcvwnd);

    hloop_t* loop = hloop_new(0);

    listen_io = hloop_create_tcp_server(loop, listen_host, listen_port, on_accept);
    if (listen_io == NULL) {
        fprintf(stderr, "create tcp server error!\n");
        return -30;
    }

    kcp_io = hloop_create_udp_client(loop, kcp_host, kcp_port);
    if (kcp_io == NULL) {
        fprintf(stderr, "create udp client error!\n");
        return -40;
    }
    hio_set_kcp(kcp_io, &s_kcp_setting);
    kcp_ctx_t* kcp_ctx = NULL;
    HV_ALLOC_SIZEOF(kcp_ctx);
    hevent_set_userdata(kcp_io, kcp_ctx);
    hio_setcb_read(kcp_io, on_kcp_recvfrom);
    hio_read(kcp_io);

    // smux
    smux_config.max_frame_size = 1024;
    smux_session.next_stream_id = 1;
    smux_stream0.stream_id = 0;

    // set heartbeat timer
    if (smux_config.keepalive_interval > 0) {
        htimer_t* timer = htimer_add(loop, send_hearbeat, smux_config.keepalive_interval, INFINITE);
        hevent_set_userdata(timer, &smux_stream0);
    }

    hloop_run(loop);
    hloop_free(&loop);
    return 0;
}
