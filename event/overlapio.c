#include "iowatcher.h"

#ifdef EVENT_IOCP
#include "overlapio.h"

#define ACCEPTEX_NUM    10

int post_acceptex(hio_t* listenio, hoverlapped_t* hovlp) {
    LPFN_ACCEPTEX AcceptEx = NULL;
    GUID guidAcceptEx = WSAID_ACCEPTEX;
    DWORD dwbytes = 0;
    if (WSAIoctl(listenio->fd, SIO_GET_EXTENSION_FUNCTION_POINTER,
        &guidAcceptEx, sizeof(guidAcceptEx),
        &AcceptEx, sizeof(AcceptEx),
        &dwbytes, NULL, NULL) != 0) {
        return WSAGetLastError();
    }
    int connfd = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (connfd < 0) {
        return WSAGetLastError();
    }
    if (hovlp == NULL) {
        hovlp = (hoverlapped_t*)malloc(sizeof(hoverlapped_t));
        memset(hovlp, 0, sizeof(hoverlapped_t));
        hovlp->buf.len = 20 + (sizeof(SOCKADDR_IN) + 16) * 2;
        hovlp->buf.buf = (char*)malloc(hovlp->buf.len);
        memset(hovlp->buf.buf, 0, hovlp->buf.len);
    }
    hovlp->fd = connfd;
    hovlp->event = READ_EVENT;
    hovlp->io = listenio;
    if (AcceptEx(listenio->fd, connfd, hovlp->buf.buf, 0, sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
        &dwbytes, &hovlp->ovlp) != TRUE) {
        int err = WSAGetLastError();
        if (err != ERROR_IO_PENDING) {
            fprintf(stderr, "AcceptEx error: %d\n", err);
            return err;
        }
    }
    return 0;
}

int post_recv(hio_t* io, hoverlapped_t* hovlp) {
    if (hovlp == NULL) {
        hovlp = (hoverlapped_t*)malloc(sizeof(hoverlapped_t));
        memset(hovlp, 0, sizeof(hoverlapped_t));
    }
    hovlp->fd = io->fd;
    hovlp->event = READ_EVENT;
    hovlp->io = io;
    hovlp->buf.buf = io->readbuf.base;
    hovlp->buf.len = io->readbuf.len;
    memset(hovlp->buf.buf, 0, hovlp->buf.len);
    DWORD dwbytes = 0;
    DWORD flags = 0;
    int ret = WSARecv(io->fd, &hovlp->buf, 1, &dwbytes, &flags, &hovlp->ovlp, NULL);
    printd("WSARecv ret=%d bytes=%u\n", ret, dwbytes);
    if (ret != 0) {
        int err = WSAGetLastError();
        if (err != ERROR_IO_PENDING) {
            fprintf(stderr, "WSARecv error: %d\n", err);
            return err;
        }
    }
    return 0;
}

static void on_acceptex_complete(hio_t* io) {
    printd("on_acceptex_complete------\n");
    hoverlapped_t* hovlp = (hoverlapped_t*)io->hovlp;
    int listenfd = io->fd;
    int connfd = hovlp->fd;
    LPFN_GETACCEPTEXSOCKADDRS GetAcceptExSockaddrs = NULL;
    GUID guidGetAcceptExSockaddrs = WSAID_GETACCEPTEXSOCKADDRS;
    DWORD dwbytes = 0;
    if (WSAIoctl(connfd, SIO_GET_EXTENSION_FUNCTION_POINTER,
        &guidGetAcceptExSockaddrs, sizeof(guidGetAcceptExSockaddrs),
        &GetAcceptExSockaddrs, sizeof(GetAcceptExSockaddrs),
        &dwbytes, NULL, NULL) != 0) {
        return;
    }
    struct sockaddr* plocaladdr = NULL;
    struct sockaddr* ppeeraddr = NULL;
    socklen_t localaddrlen;
    socklen_t peeraddrlen;
    GetAcceptExSockaddrs(hovlp->buf.buf, 0, sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
        &plocaladdr, &localaddrlen, &ppeeraddr, &peeraddrlen);
    if (io->localaddr == NULL) {
        io->localaddr = (struct sockaddr*)malloc(localaddrlen);
    }
    memcpy(io->localaddr, plocaladdr, localaddrlen);
    if (io->peeraddr == NULL) {
        io->peeraddr = (struct sockaddr*)malloc(peeraddrlen);
    }
    memcpy(io->peeraddr, ppeeraddr, peeraddrlen);
    if (io->accept_cb) {
        struct sockaddr_in* localaddr = (struct sockaddr_in*)io->localaddr;
        struct sockaddr_in* peeraddr = (struct sockaddr_in*)io->peeraddr;
        char localip[64];
        char peerip[64];
        inet_ntop(AF_INET, &localaddr->sin_addr, localip, sizeof(localip));
        inet_ntop(AF_INET, &peeraddr->sin_addr, peerip, sizeof(peerip));
        printd("accept listenfd=%d connfd=%d [%s:%d] <= [%s:%d]\n", listenfd, connfd,
                localip, ntohs(localaddr->sin_port),
                peerip, ntohs(peeraddr->sin_port));
        setsockopt(connfd, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (const char*)&listenfd, sizeof(int));
        printd("accept_cb------\n");
        io->accept_cb(io, connfd);
        printd("accept_cb======\n");
    }
    post_acceptex(io, hovlp);
}

static void on_connectex_complete(hio_t* io) {
    printd("on_connectex_complete------\n");
    hoverlapped_t* hovlp = (hoverlapped_t*)io->hovlp;
    int state = hovlp->error == 0 ? 1 : 0;
    if (state == 1) {
        if (io->localaddr == NULL) {
            io->localaddr = (struct sockaddr*)malloc(sizeof(struct sockaddr_in));
        }
        if (io->peeraddr == NULL) {
            io->peeraddr = (struct sockaddr*)malloc(sizeof(struct sockaddr_in));
        }
        setsockopt(io->fd, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);
        socklen_t addrlen = sizeof(struct sockaddr_in);
        getsockname(io->fd, io->localaddr, &addrlen);
        addrlen = sizeof(struct sockaddr_in);
        getpeername(io->fd, io->peeraddr, &addrlen);
        struct sockaddr_in* localaddr = (struct sockaddr_in*)io->localaddr;
        struct sockaddr_in* peeraddr = (struct sockaddr_in*)io->peeraddr;
        char localip[64];
        char peerip[64];
        inet_ntop(AF_INET, &localaddr->sin_addr, localip, sizeof(localip));
        inet_ntop(AF_INET, &peeraddr->sin_addr, peerip, sizeof(peerip));
        printd("connect connfd=%d [%s:%d] => [%s:%d]\n", io->fd,
                localip, ntohs(localaddr->sin_port),
                peerip, ntohs(peeraddr->sin_port));
    }
    else {
        io->error = hovlp->error;
    }
    if (io->connect_cb) {
        printd("connect_cb------\n");
        io->connect_cb(io, state);
        printd("connect_cb======\n");
    }
    SAFE_FREE(io->hovlp);
    if (state == 0) {
        hclose(io);
    }
}

static void on_wsarecv_complete(hio_t* io) {
    printd("on_recv_complete------\n");
    hoverlapped_t* hovlp = (hoverlapped_t*)io->hovlp;
    if (hovlp->bytes == 0) {
        io->error = WSAGetLastError();
        hclose(io);
        return;
    }

    if (io->read_cb) {
        printd("read_cb------\n");
        io->read_cb(io, hovlp->buf.buf, hovlp->bytes);
        printd("read_cb======\n");
    }
    if (!io->closed) {
        post_recv(io, hovlp);
    }
}

static void on_wsasend_complete(hio_t* io) {
    printd("on_send_complete------\n");
    hoverlapped_t* hovlp = (hoverlapped_t*)io->hovlp;
    if (hovlp->bytes == 0) {
        io->error = WSAGetLastError();
        hclose(io);
        goto end;
    }
    if (io->write_cb) {
        printd("write_cb------\n");
        io->write_cb(io, hovlp->buf.buf, hovlp->bytes);
        printd("write_cb======\n");
    }
end:
    if (io->hovlp) {
        SAFE_FREE(hovlp->buf.buf);
        free(io->hovlp);
        io->hovlp = NULL;
    }
}

static void hio_handle_events(hio_t* io) {
    if ((io->events & READ_EVENT) && (io->revents & READ_EVENT)) {
        if (io->accept) {
            on_acceptex_complete(io);
        }
        else {
            on_wsarecv_complete(io);
        }
    }

    if ((io->events & WRITE_EVENT) && (io->revents & WRITE_EVENT)) {
        // NOTE: WRITE_EVENT just do once
        // ONESHOT
        hio_del(io, WRITE_EVENT);
        if (io->connect) {
            io->connect = 0;

            on_connectex_complete(io);
        }
        else {
            on_wsasend_complete(io);
        }
    }

    io->revents = 0;
}

hio_t* haccept  (hloop_t* loop, int listenfd, haccept_cb accept_cb) {
    hio_t* io = hio_add(loop, hio_handle_events, listenfd, READ_EVENT);
    if (io == NULL) return NULL;
    if (accept_cb) {
        io->accept_cb = accept_cb;
    }
    io->accept = 1;
    nonblocking(listenfd);
    for (int i = 0; i < ACCEPTEX_NUM; ++i) {
        post_acceptex(io, NULL);
    }
    return io;
}

hio_t* hconnect (hloop_t* loop, const char* host, int port, hconnect_cb connect_cb) {
    // gethostbyname -> socket -> bind -> nonblocking -> ConnectEx
    struct sockaddr_in peeraddr;
    socklen_t addrlen = sizeof(struct sockaddr_in);
    memset(&peeraddr, 0, addrlen);
    peeraddr.sin_family = AF_INET;
    inet_pton(AF_INET, host, &peeraddr.sin_addr);
    if (peeraddr.sin_addr.s_addr == INADDR_NONE) {
        struct hostent* phe = gethostbyname(host);
        if (phe == NULL)    return NULL;
        memcpy(&peeraddr.sin_addr, phe->h_addr, phe->h_length);
    }
    peeraddr.sin_port = htons(port);
    int connfd = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (connfd < 0) {
        perror("socket");
        return NULL;
    }
    // NOTE: ConnectEx must call bind
    struct sockaddr_in localaddr;
    memset(&localaddr, 0, addrlen);
    localaddr.sin_family = AF_INET;
    localaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    localaddr.sin_port = htons(0);
    if (bind(connfd, (struct sockaddr*)&localaddr, addrlen) < 0) {
        perror("bind");
        closesocket(connfd);
        return NULL;
    }
    nonblocking(connfd);
    hio_t* io = hio_add(loop, hio_handle_events, connfd, WRITE_EVENT);
    if (io == NULL) {
        closesocket(connfd);
        return NULL;
    }
    if (connect_cb) {
        io->connect_cb = connect_cb;
    }
    io->connect = 1;
    // ConnectEx
    io->connectex = 1;
    LPFN_CONNECTEX ConnectEx = NULL;
    GUID guidConnectEx = WSAID_CONNECTEX;
    DWORD dwbytes;
    if (WSAIoctl(connfd, SIO_GET_EXTENSION_FUNCTION_POINTER,
        &guidConnectEx, sizeof(guidConnectEx),
        &ConnectEx, sizeof(ConnectEx),
        &dwbytes, NULL, NULL) != 0) {
        closesocket(connfd);
        return NULL;
    }
    // NOTE: free on_connectex_complete
    hoverlapped_t* hovlp = (hoverlapped_t*)malloc(sizeof(hoverlapped_t));
    memset(hovlp, 0, sizeof(hoverlapped_t));
    hovlp->fd = connfd;
    hovlp->event = WRITE_EVENT;
    hovlp->io = io;
    if (ConnectEx(connfd, (struct sockaddr*)&peeraddr, addrlen, NULL, 0, &dwbytes, &hovlp->ovlp) != TRUE) {
        int err = WSAGetLastError();
        if (err != ERROR_IO_PENDING) {
            fprintf(stderr, "AcceptEx error: %d\n", err);
            return NULL;
        }
    }
    return io;
error:
    closesocket(connfd);
    return NULL;
}

hio_t* hread (hloop_t* loop, int fd, void* buf, size_t len, hread_cb read_cb) {
    hio_t* io = hio_add(loop, hio_handle_events, fd, READ_EVENT);
    if (io == NULL) return NULL;
    io->readbuf.base = (char*)buf;
    io->readbuf.len = len;
    if (read_cb) {
        io->read_cb = read_cb;
    }
    post_recv(io, NULL);
    return io;
}

hio_t* hwrite (hloop_t* loop, int fd, const void* buf, size_t len, hwrite_cb write_cb) {
    hio_t* io = hio_add(loop, hio_handle_events, fd, 0);
    if (io == NULL) return NULL;
    if (write_cb) {
        io->write_cb = write_cb;
    }
    int nwrite = 0;
try_send:
    nwrite = send(fd, buf, len, 0);
    //printd("write retval=%d\n", nwrite);
    if (nwrite < 0) {
        if (sockerrno == NIO_EAGAIN) {
            nwrite = 0;
            goto WSASend;
        }
        else {
            perror("write");
            io->error = sockerrno;
            goto write_error;
        }
    }
    if (nwrite == 0) {
        goto disconnect;
    }
    if (write_cb) {
        printd("try_write_cb------\n");
        write_cb(io, buf, nwrite);
        printd("try_write_cb======\n");
    }
    if (nwrite == len) {
        //goto write_done;
        return io;
    }
WSASend:
    {
        hoverlapped_t* hovlp = (hoverlapped_t*)malloc(sizeof(hoverlapped_t));
        memset(hovlp, 0, sizeof(hoverlapped_t));
        hovlp->fd = fd;
        hovlp->event = WRITE_EVENT;
        hovlp->buf.len = len - nwrite;
        // NOTE: free on_send_complete
        hovlp->buf.buf = (char*)malloc(hovlp->buf.len);
        memcpy(hovlp->buf.buf, buf + nwrite, hovlp->buf.len);
        hovlp->io = io;
        DWORD dwbytes = 0;
        DWORD flags = 0;
        int ret = WSASend(fd, &hovlp->buf, 1, &dwbytes, flags, &hovlp->ovlp, NULL);
        printd("WSASend ret=%d bytes=%u\n", ret, dwbytes);
        if (ret != 0) {
            int err = WSAGetLastError();
            if (err != ERROR_IO_PENDING) {
                fprintf(stderr, "WSASend error: %d\n", err);
                return NULL;
            }
        }
        hio_add(loop, hio_handle_events, fd, WRITE_EVENT);
        return io;
    }
write_error:
disconnect:
    hclose(io);
    return io;
}

void   hclose   (hio_t* io) {
    //printd("close fd=%d\n", io->fd);
    if (io->closed) return;
#ifdef USE_DISCONNECTEX
    // DisconnectEx reuse socket
    if (io->connectex) {
        io->connectex = 0;
        LPFN_DISCONNECTEX DisconnectEx = NULL;
        GUID guidDisconnectEx = WSAID_DISCONNECTEX;
        DWORD dwbytes;
        if (WSAIoctl(io->fd, SIO_GET_EXTENSION_FUNCTION_POINTER,
            &guidDisconnectEx, sizeof(guidDisconnectEx),
            &DisconnectEx, sizeof(DisconnectEx),
            &dwbytes, NULL, NULL) != 0) {
            return;
        }
        DisconnectEx(io->fd, NULL, 0, 0);
    }
#else
    closesocket(io->fd);
#endif
    io->closed = 1;
    if (io->hovlp) {
        hoverlapped_t* hovlp = (hoverlapped_t*)io->hovlp;
        // NOTE: hread buf provided by caller
        if (hovlp->buf.buf != io->readbuf.base) {
            SAFE_FREE(hovlp->buf.buf);
        }
        free(io->hovlp);
        io->hovlp = NULL;
    }
    if (io->close_cb) {
        printd("close_cb------\n");
        io->close_cb(io);
        printd("close_cb======\n");
    }
    hio_del(io, ALL_EVENTS);
}

#endif
