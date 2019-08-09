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
        SAFE_ALLOC_SIZEOF(hovlp);
        hovlp->buf.len = 20 + sizeof(struct sockaddr_in6) * 2;
        SAFE_ALLOC(hovlp->buf.buf, hovlp->buf.len);
    }
    hovlp->fd = connfd;
    hovlp->event = READ_EVENT;
    hovlp->io = listenio;
    if (AcceptEx(listenio->fd, connfd, hovlp->buf.buf, 0, sizeof(struct sockaddr_in6), sizeof(struct sockaddr_in6),
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
        SAFE_ALLOC_SIZEOF(hovlp);
    }
    hovlp->fd = io->fd;
    hovlp->event = READ_EVENT;
    hovlp->io = io;
    hovlp->buf.buf = io->readbuf.base;
    hovlp->buf.len = io->readbuf.len;
    memset(hovlp->buf.buf, 0, hovlp->buf.len);
    DWORD dwbytes = 0;
    DWORD flags = 0;
    int ret = 0;
    if (io->io_type == HIO_TYPE_TCP) {
        ret = WSARecv(io->fd, &hovlp->buf, 1, &dwbytes, &flags, &hovlp->ovlp, NULL);
    }
    else if (io->io_type == HIO_TYPE_UDP ||
            io->io_type == HIO_TYPE_IP) {
        ret = WSARecvFrom(io->fd, &hovlp->buf, 1, &dwbytes, &flags, io->peeraddr, &io->peeraddrlen, &hovlp->ovlp, NULL);
    }
    else {
        ret = -1;
    }
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
    GetAcceptExSockaddrs(hovlp->buf.buf, 0, sizeof(struct sockaddr_in6), sizeof(struct sockaddr_in6),
        &plocaladdr, &localaddrlen, &ppeeraddr, &peeraddrlen);
    memcpy(io->localaddr, plocaladdr, localaddrlen);
    memcpy(io->peeraddr, ppeeraddr, peeraddrlen);
    if (io->accept_cb) {
        char localaddrstr[INET6_ADDRSTRLEN+16] = {0};
        char peeraddrstr[INET6_ADDRSTRLEN+16] = {0};
        printd("accept listenfd=%d connfd=%d [%s] <= [%s]\n", listenfd, connfd,
                sockaddr_snprintf(io->localaddr, localaddrstr, sizeof(localaddrstr)),
                sockaddr_snprintf(io->peeraddr, peeraddrstr, sizeof(peeraddrstr)));
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
        setsockopt(io->fd, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);
        socklen_t addrlen = sizeof(struct sockaddr_in6);
        getsockname(io->fd, io->localaddr, &addrlen);
        addrlen = sizeof(struct sockaddr_in6);
        getpeername(io->fd, io->peeraddr, &addrlen);
        char localaddrstr[INET6_ADDRSTRLEN+16] = {0};
        char peeraddrstr[INET6_ADDRSTRLEN+16] = {0};
        printd("connect connfd=%d [%s] => [%s]\n", io->fd,
                sockaddr_snprintf(io->localaddr, localaddrstr, sizeof(localaddrstr)),
                sockaddr_snprintf(io->peeraddr, peeraddrstr, sizeof(peeraddrstr)));
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
        SAFE_FREE(io->hovlp);
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

int hio_accept (hio_t* io) {
    for (int i = 0; i < ACCEPTEX_NUM; ++i) {
        post_acceptex(io, NULL);
    }
    return hio_add(io, hio_handle_events, READ_EVENT);
}

int hio_connect (hio_t* io) {
    // NOTE: ConnectEx must call bind
    struct sockaddr_in localaddr;
    socklen_t addrlen = sizeof(localaddr);
    memset(&localaddr, 0, addrlen);
    localaddr.sin_family = AF_INET;
    localaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    localaddr.sin_port = htons(0);
    if (bind(io->fd, (struct sockaddr*)&localaddr, addrlen) < 0) {
        perror("bind");
        goto error;
    }
    // ConnectEx
    io->connectex = 1;
    LPFN_CONNECTEX ConnectEx = NULL;
    GUID guidConnectEx = WSAID_CONNECTEX;
    DWORD dwbytes;
    if (WSAIoctl(io->fd, SIO_GET_EXTENSION_FUNCTION_POINTER,
        &guidConnectEx, sizeof(guidConnectEx),
        &ConnectEx, sizeof(ConnectEx),
        &dwbytes, NULL, NULL) != 0) {
        goto error;
    }
    // NOTE: free on_connectex_complete
    hoverlapped_t* hovlp;
    SAFE_ALLOC_SIZEOF(hovlp);
    hovlp->fd = io->fd;
    hovlp->event = WRITE_EVENT;
    hovlp->io = io;
    if (ConnectEx(io->fd, io->peeraddr, sizeof(struct sockaddr_in6), NULL, 0, &dwbytes, &hovlp->ovlp) != TRUE) {
        int err = WSAGetLastError();
        if (err != ERROR_IO_PENDING) {
            fprintf(stderr, "AcceptEx error: %d\n", err);
            goto error;
        }
    }
    return hio_add(io, hio_handle_events, WRITE_EVENT);
error:
    hclose(io);
    return 0;
};

int hio_read (hio_t* io) {
    post_recv(io, NULL);
    return hio_add(io, hio_handle_events, READ_EVENT);
}

int hio_write(hio_t* io, const void* buf, size_t len) {
    int nwrite = 0;
try_send:
    if (io->io_type == HIO_TYPE_TCP) {
        nwrite = send(io->fd, buf, len, 0);
    }
    else if (io->io_type == HIO_TYPE_UDP ||
             io->io_type == HIO_TYPE_IP) {
        nwrite = recvfrom(io->fd, buf, len, 0, io->peeraddr, &io->peeraddrlen);
    }
    else {
        nwrite = -1;
    }
    //printd("write retval=%d\n", nwrite);
    if (nwrite < 0) {
        if (socket_errno() == EAGAIN) {
            nwrite = 0;
            goto WSASend;
        }
        else {
            perror("write");
            io->error = socket_errno();
            goto write_error;
        }
    }
    if (nwrite == 0) {
        goto disconnect;
    }
    if (io->write_cb) {
        printd("try_write_cb------\n");
        io->write_cb(io, buf, nwrite);
        printd("try_write_cb======\n");
    }
    if (nwrite == len) {
        //goto write_done;
        return nwrite;
    }
WSASend:
    {
        hoverlapped_t* hovlp;
        SAFE_ALLOC_SIZEOF(hovlp);
        hovlp->fd = io->fd;
        hovlp->event = WRITE_EVENT;
        hovlp->buf.len = len - nwrite;
        // NOTE: free on_send_complete
        SAFE_ALLOC(hovlp->buf.buf, hovlp->buf.len);
        memcpy(hovlp->buf.buf, buf + nwrite, hovlp->buf.len);
        hovlp->io = io;
        DWORD dwbytes = 0;
        DWORD flags = 0;
        int ret = 0;
        if (io->io_type == HIO_TYPE_TCP) {
            ret = WSASend(io->fd, &hovlp->buf, 1, &dwbytes, flags, &hovlp->ovlp, NULL);
        }
        else if (io->io_type == HIO_TYPE_UDP ||
                 io->io_type == HIO_TYPE_IP) {
            ret = WSASendTo(io->fd, &hovlp->buf, 1, &dwbytes, flags, io->peeraddr, io->peeraddrlen, &hovlp->ovlp, NULL);
        }
        else {
            ret = -1;
        }
        printd("WSASend ret=%d bytes=%u\n", ret, dwbytes);
        if (ret != 0) {
            int err = WSAGetLastError();
            if (err != ERROR_IO_PENDING) {
                fprintf(stderr, "WSASend error: %d\n", err);
                return NULL;
            }
        }
        return hio_add(io, hio_handle_events, WRITE_EVENT);
    }
write_error:
disconnect:
    hclose(io);
    return 0;
}

void hio_close (hio_t* io) {
    //printd("close fd=%d\n", io->fd);
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
    if (io->hovlp) {
        hoverlapped_t* hovlp = (hoverlapped_t*)io->hovlp;
        // NOTE: hread buf provided by caller
        if (hovlp->buf.buf != io->readbuf.base) {
            SAFE_FREE(hovlp->buf.buf);
        }
        SAFE_FREE(io->hovlp);
    }
}

#endif
