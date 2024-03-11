
#include "iowatcher.h"
#ifdef EVENT_IOURING

// #define PRINT_DEBUG
// #define PRINT_ERROR

#include "hdef.h"
#include "herr.h"
#include "hevent.h"
#include "hplatform.h"
#include "hssl.h"
#include "hthread.h"
#include "hsocket.h"
#include "hlog.h"
#include <liburing.h>
#include <openssl/ssl.h>
#include <poll.h>

#define EVENTS_INIT_SIZE 1024
#define NO_FLAGS 0
#define BUFFERS_COUNT 2
#define MAX_MESSAGE_LEN HLOOP_READ_BUFSIZE
#define __MTU 1400

typedef struct {
    struct io_uring ring;
    char* writebuf;
} hio_uring_t;

typedef struct {
    struct msghdr* msg; // for dgrame and raw socket
    int buffer_gpid;
    char buffers[BUFFERS_COUNT][MAX_MESSAGE_LEN]; // 4*8192=32768
} hio_uring_buf_t;

enum EVTYPE { ACCEPT = 1, CONNECT, HANDSHAKE_READ, HANDSHAKE_WRITE, READ, WRITE, PROV_BUF, CLOSED };

typedef union {
    struct {
        int32_t fd;
        uint32_t evtype : 4;
        uint32_t buffer_id : 4;
    };
    uint64_t stub;
} request_t;

static void __on_accept_complete(hio_uring_t* ctx, hio_t* io, int connfd);
static void __on_connect_complete(hio_uring_t* ctx, hio_t* io);
static void __on_read_complete(hio_uring_t* ctx, hio_t* io, const void* buf, int nread);
static void __on_write_complete(hio_uring_t* ctx, hio_t* io, const void* buf, int nwrite);
static void __set_user_data(int fd, int type, int buffer_id, struct io_uring_sqe* sqe);
typedef void (*FUN_SSL_HANDSHAKE)(hio_uring_t* ctx, hio_t* io, const void* data, int nread);

int iowatcher_init(hloop_t* loop) {
    if (!loop->iowatcher) {
        hio_uring_t* ctx;
        HV_ALLOC_SIZEOF(ctx);
        struct io_uring_params params = {0};
        params.sq_thread_idle = 120000; // 2 minutes in ms
        int ret = io_uring_queue_init_params(EVENTS_INIT_SIZE, &ctx->ring, &params);
        if (ret < 0) {
            printe("io_uring_queue_init() failed %s\n", strerror(errno));
            return ret;
        }
        if (!(params.features & IORING_FEAT_FAST_POLL)) {
            printf("IORING_FEAT_FAST_POLL not available in the kernel, quiting...\n");
            exit(0);
        }
        // check if buffer selection is supported
        struct io_uring_probe* probe;
        probe = io_uring_get_probe_ring(&ctx->ring);
        if (!probe || !io_uring_opcode_supported(probe, IORING_OP_PROVIDE_BUFFERS)) {
            printf("Buffer select not supported, skipping...\n");
            exit(0);
        }
        io_uring_free_probe(probe);
        loop->iowatcher = ctx;
        HV_ALLOC(ctx->writebuf, MAX_MESSAGE_LEN);
    }
    return 0;
}

int iowatcher_cleanup(hloop_t* loop) {
    if (loop->iowatcher) {
        io_uring_queue_exit(loop->iowatcher);
        hio_uring_t* ctx = loop->iowatcher;
        if (ctx->writebuf) {
            HV_FREE(ctx->writebuf);
        }
        HV_FREE(loop->iowatcher);
    }
    return 0;
}

int iowatcher_add_event(hloop_t* loop, int fd, int events) {
    return 0;
}

int iowatcher_del_event(hloop_t* loop, int fd, int events) {
    return 0;
}

int iowatcher_poll_events(hloop_t* loop, int timeout) {
    unsigned head = 0;
    int ret = -1, nevents = 0;
    struct io_uring_cqe* cqe = NULL;
    hio_uring_t* ctx = (hio_uring_t*)loop->iowatcher;
    if (ctx == NULL) return 0;
    if (timeout != INFINITE) {
        struct __kernel_timespec ts;
        ts.tv_sec = timeout / 1000;
        ts.tv_nsec = (timeout - ts.tv_sec * 1000) * 1000000;
        io_uring_submit(&ctx->ring);                            // submit in poller is faster than in read ...
        ret = io_uring_wait_cqe_timeout(&ctx->ring, &cqe, &ts); // sperate submit and wait is faster
    }
    else {
        io_uring_submit(&ctx->ring);
        ret = io_uring_wait_cqe(&ctx->ring, &cqe);
    }
    if (ret < 0) {
        if (ret == -ETIME || errno == EINTR) {
            return 0;
        }
        else {
            printe("io_uring_submit_and_wait %s\n", strerror(errno));
            return ret;
        }
    }
    io_uring_for_each_cqe(&ctx->ring, head, cqe) {
        nevents++;
        // if (cqe) {
        request_t req = {.stub = io_uring_cqe_get_data64(cqe)};
        if (req.evtype == PROV_BUF) {
        }
        else {
            hio_t* io = loop->ios.ptr[req.fd];

            __s32 result = cqe->res;
            hio_uring_buf_t* buf = io->iouring_buf;
            switch (req.evtype) {
            case ACCEPT: {
                __on_accept_complete(ctx, io, result); // result is client fd
                break;
            }
            case CONNECT: {
                __on_connect_complete(ctx, io);
                break;
            }
            case HANDSHAKE_READ: {
                int buffer_id = cqe->flags >> 16;
                ((FUN_SSL_HANDSHAKE)io->cb)(ctx, io, buf->buffers[buffer_id], result);
                // __ssl_client_handshake(ctx, io, buf->buffers[buffer_id], result);
                if (result > 0) {
                    // prov buf
                    struct io_uring_sqe* sqe = io_uring_get_sqe(&ctx->ring);
                    // printf("buffer_id=%d\n", buffer_id);
                    io_uring_prep_provide_buffers(sqe, buf->buffers[buffer_id], MAX_MESSAGE_LEN, 1, buf->buffer_gpid, buffer_id);
                    __set_user_data(req.fd, PROV_BUF, buffer_id, sqe);
                }
                break;
            }
            case HANDSHAKE_WRITE: {
                ((FUN_SSL_HANDSHAKE)io->cb)(ctx, io, NULL, result);
                // __ssl_client_handshake(ctx, io, NULL, result);
                break;
            }
            case READ: {
                int buffer_id = cqe->flags >> 16;
                __on_read_complete(ctx, io, buf->buffers[buffer_id], result);
                if (result > 0) {
                    // prov buf
                    struct io_uring_sqe* sqe = io_uring_get_sqe(&ctx->ring);
                    // printf("buffer_id=%d\n", buffer_id);
                    io_uring_prep_provide_buffers(sqe, buf->buffers[buffer_id], MAX_MESSAGE_LEN, 1, buf->buffer_gpid, buffer_id);
                    __set_user_data(req.fd, PROV_BUF, buffer_id, sqe);
                }
                break;
            }
            case WRITE: {
                // write completed
                __on_write_complete(ctx, io, ctx->writebuf, result);
                break;
            }
            case PROV_BUF: {
                if (result < 0) {
                    fprintf(stderr, "PROV_BUF current_cqe->res=%d\n", result);
                    exit(1);
                }
                // printf("PROV_BUF buffer_id=%d, for fd=%d \n", req.buffer_id, req.fd);
                break;
            }
            case CLOSED: {
                printf("closed fd=%d\n", req.fd);
                break;
            }
            default: {
                fprintf(stderr, "unknow cqe %d\n", req.fd);
                printf("default");
            }
            }
        }
    }
    io_uring_cq_advance(&ctx->ring, nevents);
    return nevents;
}

////////////

static inline void __set_user_data(int fd, int type, int buffer_id, struct io_uring_sqe* sqe) {
    request_t info = {.fd = fd, .evtype = type, .buffer_id = buffer_id};
    size_t n = sizeof(info);
    assert(n <= 8);
    sqe->user_data = info.stub;
}

int __regbuffers(hio_uring_t* ctx, hio_t* io) {
    int ret = 0;
    struct io_uring_cqe* cqe;
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ctx->ring);
    hio_uring_buf_t* buf;
    HV_ALLOC_SIZEOF(buf);
    if (io->io_type & (HIO_TYPE_SOCK_RAW | HIO_TYPE_SOCK_DGRAM)) {
        HV_ALLOC_SIZEOF(buf->msg);
    }
    buf->buffer_gpid = io->id;
    io->iouring_buf = buf;
    io_uring_prep_provide_buffers(sqe, buf->buffers, MAX_MESSAGE_LEN, BUFFERS_COUNT, buf->buffer_gpid, 0);
    __set_user_data(0, PROV_BUF, 0, sqe);
    return ret;
}

int __unregbuffers(hio_uring_t* ctx, hio_t* io) {
    int ret = 0;
    struct io_uring_cqe* cqe;
    hio_uring_buf_t* buf = io->iouring_buf;
    if (buf->msg) {
        HV_FREE(buf->msg);
    }
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ctx->ring);
    io_uring_prep_remove_buffers(sqe, BUFFERS_COUNT, buf->buffer_gpid);
    __set_user_data(0, PROV_BUF, 0, sqe);
    HV_FREE(io->iouring_buf);
    return ret;
}

hio_uring_t* __get_ring_from_io(hio_t* io) {
    hloop_t* loop = hevent_loop(io);
    if (!loop->iowatcher) {
        iowatcher_init(loop);
    }
    return (hio_uring_t*)(loop->iowatcher);
}

static void __prep_write(hio_t* io, const void* buf, size_t len, enum EVTYPE evtype) {
    ///
    hio_uring_t* ctx = __get_ring_from_io(io);
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ctx->ring);
    __set_user_data(io->fd, evtype, 0, sqe);

    switch (io->io_type) {
    case HIO_TYPE_SSL: {
#if WITH_OPENSSL
        if (evtype == WRITE) {
            int n = SSL_write(io->ssl, buf, len);
            BIO* write_bio = SSL_get_wbio(io->ssl);
            len = BIO_read(write_bio, (void*)buf, MAX_MESSAGE_LEN);
        }
#endif
    }
    case HIO_TYPE_TCP: {
        int flag = 0;
#ifdef MSG_NOSIGNAL
        flag |= MSG_NOSIGNAL;
#endif
        io_uring_prep_send(sqe, io->fd, buf, len, flag);
    } break;
    case HIO_TYPE_UDP:
    case HIO_TYPE_IP:
    case HIO_TYPE_KCP:
        // printf("io_uring_prep_sendto len= %zu\n", len);
        io_uring_prep_sendto(sqe, io->fd, buf, len, 0, io->peeraddr, SOCKADDR_LEN(io->peeraddr));
        break;
    default: io_uring_prep_write(sqe, io->fd, buf, len, 0); break;
    }
    io->write_bufsize += len;
    // printd("write retval=%d\n", nwrite);
}

static int __prep_read(hio_t* io, enum EVTYPE evtype) {
    hio_uring_t* ctx = __get_ring_from_io(io);
    if (!io->iouring_buf) {
        __regbuffers(ctx, io);
    }
    hio_uring_buf_t* buf = io->iouring_buf;
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ctx->ring);
    size_t len = 0;
    if (io->read_flags & HIO_READ_UNTIL_LENGTH) {
        len = io->read_until_length - (io->readbuf.tail - io->readbuf.head);
    }
    else {
        len = io->readbuf.len - io->readbuf.tail;
    }
    len = MIN(len, MAX_MESSAGE_LEN);
    if (io->io_type & (HIO_TYPE_SOCK_RAW | HIO_TYPE_SOCK_DGRAM)) {
        memset(buf->msg, 0, sizeof(struct msghdr));
        buf->msg->msg_name = io->peeraddr;
        buf->msg->msg_namelen = sizeof(sockaddr_u);
        buf->msg->msg_controllen = 0;
        // printf("%s fd=%d io_uring_prep_recvmsg\n", __func__, hio_fd(io));
        // io_uring_prep_recvmsg_multishot is too complicated
        io_uring_prep_recvmsg(sqe, io->fd, buf->msg, MSG_TRUNC);
    }
    else if (io->io_type & HIO_TYPE_SOCK_STREAM) {
        // printf("%s fd=%d io_uring_prep_recv\n", __func__, hio_fd(io));
        io_uring_prep_recv(sqe, io->fd, NULL, len, 0);
    }
    else {
        // printf("%s fd=%d io_uring_prep_read\n", __func__, hio_fd(io));
        io_uring_prep_read(sqe, io->fd, NULL, len, 0);
    }

    io_uring_sqe_set_flags(sqe, IOSQE_BUFFER_SELECT);
    sqe->buf_group = buf->buffer_gpid;
    __set_user_data(io->fd, evtype, 0, sqe);
    return 0;
}

static void __ssl_server_handshake(hio_uring_t* ctx, hio_t* io, const void* data, int nread) {
#if WITH_OPENSSL
    int ret;
    printd("ssl server handshake... nread=%d\n", nread);
    if (nread > 0) {
        if (data) {
            BIO* read_bio = SSL_get_rbio(io->ssl);
            int written = BIO_write(read_bio, data, nread);
        }
        else {
            io->write_bufsize -= nread;
        }
    }
    if (data && nread <= 0) {
        goto FAILED;
    }
    ret = hssl_accept(io->ssl);

    BIO* write_bio = SSL_get_wbio(io->ssl);
    if (ret == HSSL_WANT_WRITE || ret == HSSL_WANT_READ || BIO_pending(write_bio)) {
        const int pending_bytes = BIO_pending(write_bio);
        if (pending_bytes > 0) {
            int nwrite = MIN(pending_bytes, MAX_MESSAGE_LEN);
            int n = BIO_read(write_bio, ctx->writebuf, nwrite);
            __prep_write(io, ctx->writebuf, nwrite, HANDSHAKE_WRITE);
        }
        else if (ret == HSSL_WANT_READ) {
            __prep_read(io, HANDSHAKE_READ);
        }
    }
    else {
        if (!ret) {
            // handshake finish
            if (SSL_is_init_finished(io->ssl)) {
                hio_del(io, HV_READ);
                printd("ssl handshake finished.\n");
                hio_accept_cb(io);
            }
        }
        else {
            // fprintf(stderr, "ssl state %s ssl_want_read=%d\n", SSL_state_string_long(io->ssl), SSL_want_read(io->ssl));
        FAILED:
            LOGE("ssl handshake failed: %d", ret);
            io->error = ERR_SSL_HANDSHAKE;
            hio_close(io);
        }
    }
#endif
}

static void __on_accept_complete(hio_uring_t* ctx, hio_t* io, int connfd) {
    hio_t* connio = NULL;
    if (connfd < 0) {
        perror("accept");
        io->error = connfd;
    }
    else {
        connio = hio_get(io->loop, connfd);
        __regbuffers(ctx, connio);
        // NOTE: inherit from listenio
        connio->accept_cb = io->accept_cb;
        connio->userdata = io->userdata;
        if (io->unpack_setting) {
            hio_set_unpack(connio, io->unpack_setting);
        }

        if (io->io_type == HIO_TYPE_SSL) {
            if (connio->ssl == NULL) {
                // io->ssl_ctx > g_ssl_ctx > hssl_ctx_new
                hssl_ctx_t ssl_ctx = NULL;
                if (io->ssl_ctx) {
                    ssl_ctx = io->ssl_ctx;
                }
                else if (g_ssl_ctx) {
                    ssl_ctx = g_ssl_ctx;
                }
                else {
                    io->ssl_ctx = ssl_ctx = hssl_ctx_new(NULL);
                    io->alloced_ssl_ctx = 1;
                }
                if (ssl_ctx == NULL) {
                    io->error = ERR_NEW_SSL_CTX;
                    goto accept_error;
                }
                else {
                    hssl_t ssl = hssl_new(ssl_ctx, -1);
                    if (ssl == NULL) {
                        io->error = ERR_NEW_SSL;
                        goto accept_error;
                    }
                    connio->ssl = ssl;
                }
            }
            hio_enable_ssl(connio);
            connio->cb = (hevent_cb)__ssl_server_handshake;
            __ssl_server_handshake(ctx, connio, NULL, 0);
        }
        else {
            // NOTE: SSL call accept_cb after handshake finished
            hio_accept_cb(connio);
        }
    }
    return;
accept_error:
    printe("accept_error \n");
    return;
}

static void __on_read_complete(hio_uring_t* ctx, hio_t* io, const void* data, int nread) {
    if (nread == -ENOBUFS) {
        goto RET;
    }
    else if (nread < 0) {
        goto read_error;
    }
    else if (nread == 0) {
        goto disconnect;
    }

    // printf("%s %d nread=%d\n", __func__, io->fd, nread);
    void* buf = io->readbuf.base + io->readbuf.tail;
    if (io->io_type == HIO_TYPE_SSL) {
#if WITH_OPENSSL
        BIO* read_bio = SSL_get_rbio(io->ssl);
        BIO_write(read_bio, data, nread);
        int read = SSL_read(io->ssl, buf, io->readbuf.len - io->readbuf.tail);
        if (read <= 0) {
            // ssl  record needs more data before decrypt so schedule another read
            goto RET;
        }
        nread = read;
#endif
    }
    else {
        memcpy(buf, data, nread);
    }
    io->readbuf.tail += nread;
    hio_handle_read(io, buf, nread);
RET:
    io->last_read_hrtime = io->loop->cur_hrtime;
    if (io->events & HV_READ) {
        hio_read(io);
    }
    return;
read_error:
disconnect:
    if (io->io_type & HIO_TYPE_SOCK_STREAM) {
        hio_close(io);
    }
}

static void __connect_cb(hio_t* io) {
    hio_del_connect_timer(io);
    hio_connect_cb(io);
}

static void __ssl_client_handshake(hio_uring_t* ctx, hio_t* io, const void* data, int nread) {
#if WITH_OPENSSL
    int ret;
    printd("ssl client handshake... %d\n", nread);
    if (nread > 0) {
        if (data) {
            BIO* read_bio = SSL_get_rbio(io->ssl);
            int written = BIO_write(read_bio, data, nread);
        }
        else {
            io->write_bufsize -= nread;
        }
    }
    if (data && nread <= 0) {
        goto FAILED;
    }
    ret = hssl_connect(io->ssl);
    printf("%s %d\n", "hssl_connect", ret);

    BIO* write_bio = SSL_get_wbio(io->ssl);
    if (ret == HSSL_WANT_WRITE || ret == HSSL_WANT_READ || BIO_pending(write_bio)) {
        const int pending_bytes = BIO_pending(write_bio);
        if (pending_bytes > 0) {
            int nwrite = MIN(pending_bytes, MAX_MESSAGE_LEN);
            int n = BIO_read(write_bio, ctx->writebuf, nwrite);
            __prep_write(io, ctx->writebuf, nwrite, HANDSHAKE_WRITE);
        }
        else if (ret == HSSL_WANT_READ) {
            __prep_read(io, HANDSHAKE_READ);
        }
    }
    else {
        if (!ret) {
            // handshake finish
            if (SSL_is_init_finished(io->ssl)) {
                hio_del(io, HV_READ);
                printd("ssl handshake finished.\n");
                __connect_cb(io);
            }
        }
        else {
            // fprintf(stderr, "ssl state %s ssl_want_read=%d\n", SSL_state_string_long(io->ssl), SSL_want_read(io->ssl));
        FAILED:
            printe("ssl handshake failed: %d", ret);
            io->error = ERR_SSL_HANDSHAKE;
            hio_close(io);
        }
    }
#endif
}

static void __on_connect_complete(hio_uring_t* ctx, hio_t* io) {
    // printd("nio_connect connfd=%d\n", io->fd);
    io->connect = 0;
    __regbuffers(ctx, io);
    socklen_t addrlen = sizeof(sockaddr_u);
    int ret = getpeername(io->fd, io->peeraddr, &addrlen);
    if (ret < 0) {
        io->error = socket_errno();
        goto connect_error;
    }
    else {
        addrlen = sizeof(sockaddr_u);
        getsockname(io->fd, io->localaddr, &addrlen);

        if (io->io_type == HIO_TYPE_SSL) {
            if (io->ssl == NULL) {
                // io->ssl_ctx > g_ssl_ctx > hssl_ctx_new
                hssl_ctx_t ssl_ctx = NULL;
                if (io->ssl_ctx) {
                    ssl_ctx = io->ssl_ctx;
                }
                else if (g_ssl_ctx) {
                    ssl_ctx = g_ssl_ctx;
                }
                else {
                    io->ssl_ctx = ssl_ctx = hssl_ctx_new(NULL);
                    io->alloced_ssl_ctx = 1;
                }
                if (ssl_ctx == NULL) {
                    io->error = ERR_NEW_SSL_CTX;
                    goto connect_error;
                }
                else {
                    hssl_t ssl = hssl_new(ssl_ctx, -1);
                    if (ssl == NULL) {
                        io->error = ERR_NEW_SSL;
                        goto connect_error;
                    }
                    io->ssl = ssl;
                }
            }
            if (io->hostname) {
                hssl_set_sni_hostname(io->ssl, io->hostname);
            }
            io->cb = (hevent_cb)__ssl_client_handshake;
            __ssl_client_handshake(ctx, io, NULL, 0);
        }
        else {
            // NOTE: SSL call connect_cb after handshake finished
            __connect_cb(io);
        }

        return;
    }

connect_error:
    printe("connfd=%d connect error: %s:%d", io->fd, socket_strerror(io->error), io->error);
    hio_close(io);
}

static void __write_cb(hio_t* io, const void* buf, int nwrite) {
    // printd("< %.*s\n", writebytes, buf);
    io->write_bufsize -= nwrite;
    io->last_write_hrtime = io->loop->cur_hrtime;
    hio_write_cb(io, buf, nwrite);
}

static void __on_write_complete(hio_uring_t* ctx, hio_t* io, const void* buf, int nwrite) {
    // printd("nio_write fd=%d\n", io->fd);
    // 1. callback
    __write_cb(io, buf, nwrite);
    // 2. write next
    hrecursive_mutex_lock(&io->write_mutex);
write:
    nwrite = 0;
    if (write_queue_empty(&io->write_queue)) {
        hrecursive_mutex_unlock(&io->write_mutex);
        if (io->close) {
            io->close = 0;
            hio_close(io);
        }
        return;
    }
    else {
        int maxpacketlen = (io->io_type & (HIO_TYPE_SOCK_DGRAM | HIO_TYPE_SOCK_RAW)) ? __MTU : MAX_MESSAGE_LEN;
        while (!write_queue_empty(&io->write_queue)) {
            offset_buf_t* pbuf = write_queue_front(&io->write_queue);
            char* base = pbuf->base;
            char* buf = base + pbuf->offset;
            int len = pbuf->len - pbuf->offset;
            if (nwrite + len >= maxpacketlen) {
                len = maxpacketlen - nwrite;
                pbuf->offset = len;
                memcpy(ctx->writebuf + nwrite, buf, len);
                nwrite += len;
            }
            else {
                memcpy(ctx->writebuf + nwrite, buf, len);
                nwrite += len;
                HV_FREE(base);
                write_queue_pop_front(&io->write_queue);
            }
        }
        __prep_write(io, ctx->writebuf, nwrite, WRITE);
        // printd("write retval=%d\n", nwrite);
        hrecursive_mutex_unlock(&io->write_mutex);
        return;
    }
}

static void __connect_timeout_cb(htimer_t* timer) {
    hio_t* io = (hio_t*)timer->privdata;
    if (io) {
        char localaddrstr[SOCKADDR_STRLEN] = {0};
        char peeraddrstr[SOCKADDR_STRLEN] = {0};
        hlogw("connect timeout [%s] <=> [%s]", SOCKADDR_STR(io->localaddr, localaddrstr), SOCKADDR_STR(io->peeraddr, peeraddrstr));
        io->error = ETIMEDOUT;
        hio_close(io);
    }
}

static void __close_timeout_cb(htimer_t* timer) {
    hio_t* io = (hio_t*)timer->privdata;
    if (io) {
        char localaddrstr[SOCKADDR_STRLEN] = {0};
        char peeraddrstr[SOCKADDR_STRLEN] = {0};
        printe("close timeout [%s] <=> [%s]", SOCKADDR_STR(io->localaddr, localaddrstr), SOCKADDR_STR(io->peeraddr, peeraddrstr));
        io->error = ETIMEDOUT;
        hio_close(io);
    }
}

static void __close_cb(hio_t* io) {
    // printd("close fd=%d\n", io->fd);
    hio_del_connect_timer(io);
    hio_del_close_timer(io);
    hio_del_read_timer(io);
    hio_del_write_timer(io);
    hio_del_keepalive_timer(io);
    hio_del_heartbeat_timer(io);
    hio_close_cb(io);
}

//-------- begin hio api --------
int hio_accept(hio_t* io) {
    hio_uring_t* ctx = __get_ring_from_io(io);
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ctx->ring);
    socklen_t addrlen = sizeof(sockaddr_u);
    io_uring_prep_multishot_accept(sqe, io->fd, io->peeraddr, &addrlen, 0);
    io_uring_sqe_set_flags(sqe, NO_FLAGS);
    // userdata to differentiate between accept CQEs and others
    __set_user_data(io->fd, ACCEPT, 0, sqe);
    io->accept = 1;
    return hio_add(io, NULL, HV_READ);
}

int hio_connect(hio_t* io) {
    hio_uring_t* ctx = __get_ring_from_io(io);
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ctx->ring);
    io_uring_prep_connect(sqe, io->fd, io->peeraddr, SOCKADDR_LEN(io->peeraddr));
    __set_user_data(io->fd, CONNECT, 0, sqe);

    int timeout = io->connect_timeout ? io->connect_timeout : HIO_DEFAULT_CONNECT_TIMEOUT;
    io->connect_timer = htimer_add(io->loop, __connect_timeout_cb, timeout, 1);
    io->connect_timer->privdata = io;
    io->connect = 1;
    return hio_add(io, NULL, HV_WRITE);
}

int hio_read(hio_t* io) {
    if (io->closed) {
        printe("hio_read called but fd[%d] already closed!", io->fd);
        return -1;
    }
    __prep_read(io, READ);
    return hio_add(io, NULL, HV_READ);
}

int hio_write(hio_t* io, const void* buf, size_t len) {
    int nwrite = 0, err = 0, sendasync = 0;
    if (io->closed) {
        printe("hio_write called but fd[%d] already closed!", io->fd);
        return -1;
    }
    hio_uring_t* ctx = __get_ring_from_io(io);
    hrecursive_mutex_lock(&io->write_mutex);
#if WITH_KCP
    if (io->io_type == HIO_TYPE_KCP) {
        nwrite = hio_write_kcp(io, buf, len);
        // if (nwrite < 0) goto write_error;
        goto write_done;
    }
#endif

    if (io->write_bufsize == 0) {
        int maxpacketlen = (io->io_type & (HIO_TYPE_SOCK_DGRAM | HIO_TYPE_SOCK_RAW)) ? __MTU : MAX_MESSAGE_LEN;
        nwrite = MIN(len, maxpacketlen);
        memcpy(ctx->writebuf, buf, nwrite);
        __prep_write(io, ctx->writebuf, nwrite, WRITE);
    }
    if (nwrite < len) {
        if (io->write_bufsize + len - nwrite > io->max_write_bufsize) {
            hloge("write bufsize > %u, close it!", io->max_write_bufsize);
            io->error = ERR_OVER_LIMIT;
            goto write_error;
        }
        else {
            offset_buf_t remain;
            remain.len = len - nwrite;
            remain.offset = 0;
            // NOTE: free in nio_write
            HV_ALLOC(remain.base, remain.len);
            memcpy(remain.base, ((char*)buf) + nwrite, remain.len);
            if (io->write_queue.maxsize == 0) {
                write_queue_init(&io->write_queue, 4);
            }
            write_queue_push_back(&io->write_queue, &remain);
            io->write_bufsize += remain.len;
            if (io->write_bufsize > WRITE_BUFSIZE_HIGH_WATER) {
                hlogw("write len=%d enqueue %u, bufsize=%u over high water %u", len, (unsigned int)(remain.len - remain.offset),
                       (unsigned int)io->write_bufsize, (unsigned int)WRITE_BUFSIZE_HIGH_WATER);
            }
        }
    }
write_error:
    hrecursive_mutex_unlock(&io->write_mutex);
    hio_add(io, NULL, HV_WRITE);
    return 0;
}

int hio_close(hio_t* io) {
    if (io->closed) return 0;
    if (hv_gettid() != io->loop->tid) {
        return hio_close_async(io);
    }

    hrecursive_mutex_lock(&io->write_mutex);
    if (io->closed) {
        hrecursive_mutex_unlock(&io->write_mutex);
        return 0;
    }
    if (!write_queue_empty(&io->write_queue) && io->error == 0 && io->close == 0) {
        int timeout_ms;
        io->close = 1;
        hrecursive_mutex_unlock(&io->write_mutex);
        printe("write_queue not empty, close later.");
        timeout_ms = io->close_timeout ? io->close_timeout : HIO_DEFAULT_CLOSE_TIMEOUT;
        io->close_timer = htimer_add(io->loop, __close_timeout_cb, timeout_ms, 1);
        io->close_timer->privdata = io;
        return 0;
    }
    io->closed = 1;
    hrecursive_mutex_unlock(&io->write_mutex);
    if (io->iouring_buf) {
        hio_uring_t* ctx = __get_ring_from_io(io);
        __unregbuffers(ctx, io);
    }
    hio_done(io);
    __close_cb(io);
    if (io->ssl) {
        hssl_free(io->ssl);
        io->ssl = NULL;
    }
    if (io->ssl_ctx && io->alloced_ssl_ctx) {
        hssl_ctx_free(io->ssl_ctx);
        io->ssl_ctx = NULL;
    }
    SAFE_FREE(io->hostname);
    if (io->io_type & HIO_TYPE_SOCKET) {
        closesocket(io->fd);
    }
    return 0;
}

#endif // EVENT_IOURING
