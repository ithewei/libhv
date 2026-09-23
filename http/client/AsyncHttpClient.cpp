#include "AsyncHttpClient.h"

namespace hv {

int AsyncHttpClient::send(const HttpRequestPtr& req, HttpResponseCallback resp_cb) {
    hloop_t* loop = EventLoopThread::hloop();
    if (loop == NULL) return -1;
    auto task = std::make_shared<HttpClientTask>();
    task->req = req;
    task->cb = std::move(resp_cb);
    task->start_time = hloop_now_hrtime(loop);
    if (req->retry_count > 0 && req->retry_delay > 0) {
        req->retry_count = MIN(req->retry_count, req->timeout * 1000 / req->retry_delay - 1);
    }
    return send(task);
}

int AsyncHttpClient::checkTaskCancelOrTimeout(const HttpClientTaskPtr& task) {
    const HttpRequestPtr& req = task->req;
    if (req->cancel) {
        return -1;
    }

    uint64_t now_hrtime = hloop_now_hrtime(EventLoopThread::hloop());
    int elapsed = (now_hrtime - task->start_time) / 1000;
    int timeout_ms = req->timeout * 1000;
    if (timeout_ms > 0 && elapsed >= timeout_ms) {
        hlogw("%s queueInLoop timeout!", req->url.c_str());
        return -10;
    }
    return elapsed;
}

// ParseUrl -> if conn_key in conn_pools: startTask -> sendRequest
//          -> else: resolveDns -> startConnect
int AsyncHttpClient::doTask(const HttpClientTaskPtr& task) {
    int elapsed_ms = checkTaskCancelOrTimeout(task);
    if (elapsed_ms < 0) {
        return elapsed_ms;
    }

    const HttpRequestPtr& req = task->req;
    req->ParseUrl();
    HttpConnKey conn_key(*req);

    // The pool is keyed by the logical transport identity (host/port/proxy/TLS),
    // not the resolved sockaddr. Reuse an idle connection before DNS so a pool
    // hit never waits for or starts an unnecessary resolver query.
    int connfd = -1;
    auto iter = conn_pools.find(conn_key);
    if (iter != conn_pools.end()) {
        iter->second.get(connfd);
    }
    if (connfd >= 0) {
        const SocketChannelPtr& channel = getChannel(connfd);
        if (channel) {
            int err = startTask(task, channel);
            if (err != 0) {
                return err;
            }
            return sendRequest(channel);
        }
    }

    // Where to open the TCP connection: normally the origin, but for an HTTP
    // CONNECT tunnel (https over proxy) it is the proxy. The origin is then
    // reached via the proxy's CONNECT (see startConnect / hio_set_proxy).
    const char* host = req->host.c_str();
    int port = req->port;
    if (req->IsTunnelProxy()) {
        host = req->tunnel_proxy_host.c_str();
        port = req->tunnel_proxy_port;
    }

    // If host is a numeric IP (or UDS), resolve synchronously (fast path).
    // Otherwise resolve the hostname asynchronously via EventLoop::resolveDns
    // so the event loop is never blocked by getaddrinfo. resolveDns returns a
    // use-after-free-proof DnsID and owns the underlying hdns_t lifetime.
    if (port < 0 || is_ipaddr(host)) {
        sockaddr_u peeraddr;
        memset(&peeraddr, 0, sizeof(peeraddr));
        int ret = sockaddr_set_ipport(&peeraddr, host, port);
        if (ret != 0) {
            hloge("unknown host %s", host);
            return -20;
        }
        return startConnect(task, &peeraddr);
    }

    hdns_setting_t opt;
    if (req->connect_timeout > 0) opt.timeout_ms = req->connect_timeout * 1000;
    DnsID id = EventLoopThread::loop()->resolveDns(host,
        [this, task, port](int status, int naddrs, const sockaddr_u* addrs) {
            if (status != HDNS_STATUS_OK || naddrs <= 0) {
                hloge("resolve host %s failed, status=%d", task->req->host.c_str(), status);
                if (task->cb) task->cb(NULL);
                return;
            }
            sockaddr_u peeraddr = addrs[0];
            sockaddr_set_port(&peeraddr, port);
            int err = startConnect(task, &peeraddr);
            if (err != 0 && task->cb) {
                task->cb(NULL);
            }
        }, &opt);
    if (id == INVALID_DNS_ID) {
        hloge("hdns_resolve failed for host %s", host);
        return -20;
    }
    return 0;
}

// socket -> addChannel -> configure callbacks -> startTask -> startConnect
int AsyncHttpClient::startConnect(const HttpClientTaskPtr& task, const sockaddr_u* paddr) {
    int elapsed_ms = checkTaskCancelOrTimeout(task);
    if (elapsed_ms < 0) {
        return elapsed_ms;
    }

    const HttpRequestPtr& req = task->req;
    sockaddr_u peeraddr = *paddr;
    HttpConnKey conn_key(*req);

    // create socket
    int connfd = socket(peeraddr.sa.sa_family, SOCK_STREAM, 0);
    if (connfd < 0) {
        perror("socket");
        return -30;
    }
    hio_t* connio = hio_get(EventLoopThread::hloop(), connfd);
    assert(connio != NULL);
    hio_set_peeraddr(connio, &peeraddr.sa, sockaddr_len(&peeraddr));
    const SocketChannelPtr& channel = addChannel(connio);
    channel->getContext<HttpClientContext>()->conn_key = conn_key;
    // https over proxy: HTTP CONNECT tunnel to the origin, then TLS with it.
    if (req->IsTunnelProxy()) {
        proxy_setting_t proxy;
        proxy.protocol = PROXY_PROTOCOL_HTTP_CONNECT;
        hv_strncpy(proxy.target_host, req->host.c_str(), sizeof(proxy.target_host));
        proxy.target_port = req->port;
        if (!req->tunnel_proxy_username.empty()) {
            hv_strncpy(proxy.username, req->tunnel_proxy_username.c_str(), sizeof(proxy.username));
            hv_strncpy(proxy.password, req->tunnel_proxy_password.c_str(), sizeof(proxy.password));
        }
        hio_set_proxy(connio, &proxy);
    }
    // https: enable TLS against the origin (also for the tunnel case, run
    // after the CONNECT handshake completes, with SNI = origin host).
    if (req->IsHttps()) {
        hio_enable_ssl(connio);
        if (!is_ipaddr(req->host.c_str())) {
            hio_set_hostname(connio, req->host.c_str());
        }
    }

    channel->onconnect = [&channel]() {
        sendRequest(channel);
    };
    channel->onread = [this, &channel](Buffer* buf) {
        HttpClientContext* ctx = channel->getContext<HttpClientContext>();
        if (ctx->task == NULL) return;
        if (ctx->task->req->cancel) {
            channel->close();
            return;
        }
        const char* data = (const char*)buf->data();
        int len = buf->size();
        int nparse = ctx->parser->FeedRecvData(data, len);
        if (nparse != len) {
            ctx->errorCallback();
            channel->close();
            return;
        }
        // HTTP2: flush frames nghttp2 queued while consuming input (SETTINGS/
        // PING ACK, WINDOW_UPDATE, and request-body DATA deferred by flow
        // control until the peer's WINDOW_UPDATE arrived).
        if (ctx->task->req->http_major == 2 && ctx->parser->WantSend()) {
            char* sdata = NULL;
            size_t slen = 0;
            while (ctx->parser->GetSendData(&sdata, &slen) > 0) {
                if (sdata && slen) channel->write(sdata, slen);
            }
        }
        if (ctx->parser->IsComplete()) {
            auto& req = ctx->task->req;
            auto& resp = ctx->resp;
            bool keepalive = req->IsKeepAlive() && resp->IsKeepAlive();
            if (req->redirect && HTTP_STATUS_IS_REDIRECT(resp->status_code)) {
                std::string location = resp->headers["Location"];
                if (!location.empty()) {
                    hlogi("redirect %s => %s", req->url.c_str(), location.c_str());
                    req->url = location;
                    req->ParseUrl();
                    req->headers["Host"] = req->host;
                    resp->Reset();
                    send(ctx->task);
                    // NOTE: detatch from original channel->context
                    ctx->cancelTask();
                }
            } else {
                ctx->successCallback();
            }
            if (keepalive) {
                // NOTE: add into conn_pools to reuse
                // hlogd("add into conn_pools");
                conn_pools[ctx->conn_key].add(channel->fd());
            } else {
                channel->close();
            }
        }
    };
    channel->onclose = [this, &channel]() {
        HttpClientContext* ctx = channel->getContext<HttpClientContext>();
        // NOTE: remove from conn_pools
        // hlogd("remove from conn_pools");
        auto iter = conn_pools.find(ctx->conn_key);
        if (iter != conn_pools.end()) {
            iter->second.remove(channel->fd());
            if (iter->second.size() == 0) {
                conn_pools.erase(iter);
            }
        }

        const HttpClientTaskPtr& task = ctx->task;
        if (task) {
            if (ctx->parser &&
                ctx->parser->IsEof()) {
                ctx->successCallback();
            }
            else if (task->req &&
                     task->req->cancel == 0 &&
                     task->req->retry_count-- > 0) {
                if (task->req->retry_delay > 0) {
                    // try again after delay
                    setTimeout(task->req->retry_delay, [this, task](TimerID timerID){
                        hlogi("retry %s %s", http_method_str(task->req->method), task->req->url.c_str());
                        sendInLoop(task);
                    });
                } else {
                    send(task);
                }
            }
            else {
                ctx->errorCallback();
            }
        }

        removeChannel(channel);
    };

    int err = startTask(task, channel);
    if (err != 0) {
        return err;
    }

    if (req->connect_timeout > 0) {
        channel->setConnectTimeout(req->connect_timeout * 1000);
    }
    return channel->startConnect();
}

int AsyncHttpClient::startTask(const HttpClientTaskPtr& task,
                               const SocketChannelPtr& channel) {
    int elapsed_ms = checkTaskCancelOrTimeout(task);
    if (elapsed_ms < 0) {
        return elapsed_ms;
    }

    const HttpRequestPtr& req = task->req;
    int timeout_ms = req->timeout * 1000;

    assert(channel != NULL);
    HttpClientContext* ctx = channel->getContext<HttpClientContext>();
    ctx->task = task;

    // timer
    if (timeout_ms > 0) {
        ctx->timerID = setTimeout(timeout_ms - elapsed_ms, [&channel](TimerID timerID){
            HttpClientContext* ctx = channel->getContext<HttpClientContext>();
            if (ctx && ctx->task) {
                hlogw("%s timeout!", ctx->task->req->url.c_str());
            }
            if (channel) {
                channel->close();
            }
        });
    }

    return 0;
}

// InitResponse => SubmitRequest => while(GetSendData) write => startRead
int AsyncHttpClient::sendRequest(const SocketChannelPtr& channel) {
    HttpClientContext* ctx = (HttpClientContext*)channel->context();
    assert(ctx != NULL && ctx->task != NULL);
    if (ctx->resp == NULL) {
        ctx->resp = std::make_shared<HttpResponse>();
    }
    HttpRequest* req = ctx->task->req.get();
    HttpResponse* resp = ctx->resp.get();
    assert(req != NULL && resp != NULL);
    if (req->http_cb) resp->http_cb = std::move(req->http_cb);

    if (ctx->parser == NULL) {
        ctx->parser.reset(HttpParser::New(HTTP_CLIENT, (http_version)req->http_major));
    }
    ctx->parser->InitResponse(resp);
    ctx->parser->SubmitRequest(req);

    char* data = NULL;
    size_t len = 0;
    while (ctx->parser->GetSendData(&data, &len)) {
        if (req->cancel) {
            channel->close();
            return -1;
        }
        // NOTE: ensure write buffer size is enough
        if (len > (1 << 22) /* 4M */) {
            channel->setMaxWriteBufsize(len);
        }
        channel->write(data, len);
    }
    channel->startRead();

    return 0;
}

}
