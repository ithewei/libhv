// WARN: overlapio maybe need MemoryPool to avoid alloc/free
#include "iowatcher.h"

#ifdef EVENT_IOCP
#include "overlapio.h"
#include "hevent.h"

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
        HV_ALLOC_SIZEOF(hovlp);
        hovlp->buf.len = 20 + sizeof(struct sockaddr_in6) * 2;
        HV_ALLOC(hovlp->buf.buf, hovlp->buf.len);
    }
    hovlp->fd = connfd;
    hovlp->event = HV_READ;
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
        HV_ALLOC_SIZEOF(hovlp);
    }
    hovlp->fd = io->fd;
    hovlp->event = HV_READ;
    hovlp->io = io;
    hovlp->buf.len = io->readbuf.len;
    if (io->io_type == HIO_TYPE_UDP || io->io_type == HIO_TYPE_IP) {
        HV_ALLOC(hovlp->buf.buf, hovlp->buf.len);
    }
    else {
        hovlp->buf.buf = io->readbuf.base;
    }
    //memset(hovlp->buf.buf, 0, hovlp->buf.len);
    DWORD dwbytes = 0;
    DWORD flags = 0;
    int ret = 0;
    if (io->io_type == HIO_TYPE_TCP) {
        ret = WSARecv(io->fd, &hovlp->buf, 1, &dwbytes, &flags, &hovlp->ovlp, NULL);
    }
    else if (io->io_type == HIO_TYPE_UDP ||
            io->io_type == HIO_TYPE_IP) {
        if (hovlp->addr == NULL) {
            hovlp->addrlen = sizeof(struct sockaddr_in6);
            HV_ALLOC(hovlp->addr, sizeof(struct sockaddr_in6));
        }
        ret = WSARecvFrom(io->fd, &hovlp->buf, 1, &dwbytes, &flags, hovlp->addr, &hovlp->addrlen, &hovlp->ovlp, NULL);
    }
    else {
        ret = -1;
    }
    //printd("WSARecv ret=%d bytes=%u\n", ret, dwbytes);
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
        setsockopt(connfd, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (const char*)&listenfd, sizeof(int));
        hio_t* connio = hio_get(io->loop, connfd);
        connio->userdata = io->userdata;
        memcpy(connio->localaddr, io->localaddr, localaddrlen);
        memcpy(connio->peeraddr, io->peeraddr, peeraddrlen);
        /*
        char localaddrstr[SOCKADDR_STRLEN] = {0};
        char peeraddrstr[SOCKADDR_STRLEN] = {0};
        printd("accept listenfd=%d connfd=%d [%s] <= [%s]\n", listenfd, connfd,
                SOCKADDR_STR(connio->localaddr, localaddrstr),
                SOCKADDR_STR(connio->peeraddr, peeraddrstr));
        */
        //printd("accept_cb------\n");
        io->accept_cb(connio);
        //printd("accept_cb======\n");
    }
    post_acceptex(io, hovlp);
}

static void on_connectex_complete(hio_t* io) {
    printd("on_connectex_complete------\n");
    hoverlapped_t* hovlp = (hoverlapped_t*)io->hovlp;
    io->error = hovlp->error;
    HV_FREE(io->hovlp);
    if (io->error != 0) {
        hio_close(io);
        return;
    }
    if (io->connect_cb) {
        setsockopt(io->fd, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);
        socklen_t addrlen = sizeof(struct sockaddr_in6);
        getsockname(io->fd, io->localaddr, &addrlen);
        addrlen = sizeof(struct sockaddr_in6);
        getpeername(io->fd, io->peeraddr, &addrlen);
        /*
        char localaddrstr[SOCKADDR_STRLEN] = {0};
        char peeraddrstr[SOCKADDR_STRLEN] = {0};
        printd("connect connfd=%d [%s] => [%s]\n", io->fd,
                SOCKADDR_STR(io->localaddr, localaddrstr),
                SOCKADDR_STR(io->peeraddr, peeraddrstr));
        */
        //printd("connect_cb------\n");
        io->connect_cb(io);
        //printd("connect_cb======\n");
    }
}

static void on_wsarecv_complete(hio_t* io) {
    printd("on_recv_complete------\n");
    hoverlapped_t* hovlp = (hoverlapped_t*)io->hovlp;
    if (hovlp->bytes == 0) {
        io->error = WSAGetLastError();
        hio_close(io);
        return;
    }

    if (io->read_cb) {
        if (io->io_type == HIO_TYPE_UDP || io->io_type == HIO_TYPE_IP) {
            if (hovlp->addr && hovlp->addrlen) {
                hio_set_peeraddr(io, hovlp->addr, hovlp->addrlen);
            }
        }
        //printd("read_cb------\n");
        io->read_cb(io, hovlp->buf.buf, hovlp->bytes);
        //printd("read_cb======\n");
    }

    if (io->io_type == HIO_TYPE_TCP) {
        // reuse hovlp
        if (!io->closed) {
            post_recv(io, hovlp);
        }
    }
    else if (io->io_type == HIO_TYPE_UDP ||
            io->io_type == HIO_TYPE_IP) {
        HV_FREE(hovlp->buf.buf);
        HV_FREE(hovlp->addr);
        HV_FREE(io->hovlp);
    }
}

static void on_wsasend_complete(hio_t* io) {
    printd("on_send_complete------\n");
    hoverlapped_t* hovlp = (hoverlapped_t*)io->hovlp;
    if (hovlp->bytes == 0) {
        io->error = WSAGetLastError();
        hio_close(io);
        goto end;
    }
    if (io->write_cb) {
        if (io->io_type == HIO_TYPE_UDP || io->io_type == HIO_TYPE_IP) {
            if (hovlp->addr) {
                hio_set_peeraddr(io, hovlp->addr, hovlp->addrlen);
            }
        }
        //printd("write_cb------\n");
        io->write_cb(io, hovlp->buf.buf, hovlp->bytes);
        //printd("write_cb======\n");
    }
end:
    if (io->hovlp) {
        HV_FREE(hovlp->buf.buf);
        HV_FREE(io->hovlp);
    }
}

static void hio_handle_events(hio_t* io) {
    if ((io->events & HV_READ) && (io->revents & HV_READ)) {
        if (io->accept) {
            on_acceptex_complete(io);
        }
        else {
            on_wsarecv_complete(io);
        }
    }

    if ((io->events & HV_WRITE) && (io->revents & HV_WRITE)) {
        // NOTE: HV_WRITE just do once
        // ONESHOT
        iowatcher_del_event(io->loop, io->fd, HV_WRITE);
        io->events &= ~HV_WRITE;
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
    io->accept = 1;
    return hio_add(io, hio_handle_events, HV_READ);
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
    HV_ALLOC_SIZEOF(hovlp);
    hovlp->fd = io->fd;
    hovlp->event = HV_WRITE;
    hovlp->io = io;
    if (ConnectEx(io->fd, io->peeraddr, sizeof(struct sockaddr_in6), NULL, 0, &dwbytes, &hovlp->ovlp) != TRUE) {
        int err = WSAGetLastError();
        if (err != ERROR_IO_PENDING) {
            fprintf(stderr, "AcceptEx error: %d\n", err);
            goto error;
        }
    }
    io->connect = 1;
    return hio_add(io, hio_handle_events, HV_WRITE);
error:
    hio_close(io);
    return 0;
}

int hio_read (hio_t* io) {
    post_recv(io, NULL);
    return hio_add(io, hio_handle_events, HV_READ);
}

int hio_write(hio_t* io, const void* buf, size_t len) {
    int nwrite = 0;
try_send:
    if (io->io_type == HIO_TYPE_TCP) {
        nwrite = send(io->fd, buf, len, 0);
    }
    else if (io->io_type == HIO_TYPE_UDP) {
        nwrite = sendto(io->fd, buf, len, 0, io->peeraddr, sizeof(struct sockaddr_in6));
    }
    else if (io->io_type == HIO_TYPE_IP) {
        goto WSASend;
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
        //printd("try_write_cb------\n");
        io->write_cb(io, buf, nwrite);
        //printd("try_write_cb======\n");
    }
    if (nwrite == len) {
        //goto write_done;
        return nwrite;
    }
WSASend:
    {
        hoverlapped_t* hovlp;
        HV_ALLOC_SIZEOF(hovlp);
        hovlp->fd = io->fd;
        hovlp->event = HV_WRITE;
        hovlp->buf.len = len - nwrite;
        // NOTE: free on_send_complete
        HV_ALLOC(hovlp->buf.buf, hovlp->buf.len);
        memcpy(hovlp->buf.buf, ((char*)buf) + nwrite, hovlp->buf.len);
        hovlp->io = io;
        DWORD dwbytes = 0;
        DWORD flags = 0;
        int ret = 0;
        if (io->io_type == HIO_TYPE_TCP) {
            ret = WSASend(io->fd, &hovlp->buf, 1, &dwbytes, flags, &hovlp->ovlp, NULL);
        }
        else if (io->io_type == HIO_TYPE_UDP ||
                 io->io_type == HIO_TYPE_IP) {
            ret = WSASendTo(io->fd, &hovlp->buf, 1, &dwbytes, flags, io->peeraddr, sizeof(struct sockaddr_in6), &hovlp->ovlp, NULL);
        }
        else {
            ret = -1;
        }
        //printd("WSASend ret=%d bytes=%u\n", ret, dwbytes);
        if (ret != 0) {
            int err = WSAGetLastError();
            if (err != ERROR_IO_PENDING) {
                fprintf(stderr, "WSASend error: %d\n", err);
                return ret;
            }
        }
        return hio_add(io, hio_handle_events, HV_WRITE);
    }
write_error:
disconnect:
    hio_close(io);
    return 0;
}

int hio_close (hio_t* io) {
    if (io->closed) return 0;
    io->closed = 1;
    hio_done(io);
    if (io->hovlp) {
        hoverlapped_t* hovlp = (hoverlapped_t*)io->hovlp;
        // NOTE: hread buf provided by caller
        if (hovlp->buf.buf != io->readbuf.base) {
            HV_FREE(hovlp->buf.buf);
        }
        HV_FREE(hovlp->addr);
        HV_FREE(io->hovlp);
    }
    if (io->close_cb) {
        //printd("close_cb------\n");
        io->close_cb(io);
        //printd("close_cb======\n");
    }
    if (io->io_type & HIO_TYPE_SOCKET) {
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
    }
    return 0;
}

#endif
