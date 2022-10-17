#include "AsyncHttpClient.h"

namespace hv {

// createsocket => startConnect =>
// onconnect => sendRequest => startRead =>
// onread => HttpParser => resp_cb
int AsyncHttpClient::doTask(const HttpClientTaskPtr& task) {
    const HttpRequestPtr& req = task->req;
    // queueInLoop timeout?
    uint64_t now_hrtime = hloop_now_hrtime(EventLoopThread::hloop());
    int elapsed_ms = (now_hrtime - task->start_time) / 1000;
    int timeout_ms = req->timeout * 1000;
    if (timeout_ms > 0 && elapsed_ms >= timeout_ms) {
        hlogw("%s queueInLoop timeout!", req->url.c_str());
        return -10;
    }

    req->ParseUrl();
    sockaddr_u peeraddr;
    memset(&peeraddr, 0, sizeof(peeraddr));
    const char* host = req->host.c_str();
    int ret = sockaddr_set_ipport(&peeraddr, host, req->port);
    if (ret != 0) {
        hloge("unknown host %s", host);
        return -20;
    }

    int connfd = -1;
    // first get from conn_pools
    char strAddr[SOCKADDR_STRLEN] = {0};
    SOCKADDR_STR(&peeraddr, strAddr);
    auto iter = conn_pools.find(strAddr);
    if (iter != conn_pools.end()) {
        // hlogd("get from conn_pools");
        iter->second.get(connfd);
    }

    if (connfd < 0) {
        // create socket
        connfd = socket(peeraddr.sa.sa_family, SOCK_STREAM, 0);
        if (connfd < 0) {
            perror("socket");
            return -30;
        }
        hio_t* connio = hio_get(EventLoopThread::hloop(), connfd);
        assert(connio != NULL);
        hio_set_peeraddr(connio, &peeraddr.sa, sockaddr_len(&peeraddr));
        addChannel(connio);
        // https
        if (req->IsHttps() && !req->IsProxy()) {
            hio_enable_ssl(connio);
            if (!is_ipaddr(host)) {
                hio_set_hostname(connio, host);
            }
        }
    }

    const SocketChannelPtr& channel = getChannel(connfd);
    assert(channel != NULL);
    HttpClientContext* ctx = channel->getContext<HttpClientContext>();
    ctx->task = task;
    channel->onconnect = [&channel]() {
        sendRequest(channel);
    };
    channel->onread = [this, &channel](Buffer* buf) {
        HttpClientContext* ctx = channel->getContext<HttpClientContext>();
        if (ctx->task == NULL) return;
        const char* data = (const char*)buf->data();
        int len = buf->size();
        int nparse = ctx->parser->FeedRecvData(data, len);
        if (nparse != len) {
            ctx->errorCallback();
            channel->close();
            return;
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
                }
            } else {
                ctx->successCallback();
            }
            if (keepalive) {
                // NOTE: add into conn_pools to reuse
                // hlogd("add into conn_pools");
                conn_pools[channel->peeraddr()].add(channel->fd());
            } else {
                channel->close();
            }
        }
    };
    channel->onclose = [this, &channel]() {
        HttpClientContext* ctx = channel->getContext<HttpClientContext>();
        // NOTE: remove from conn_pools
        // hlogd("remove from conn_pools");
        auto iter = conn_pools.find(channel->peeraddr());
        if (iter != conn_pools.end()) {
            iter->second.remove(channel->fd());
        }
        const HttpClientTaskPtr& task = ctx->task;
        if (task && task->req && task->req->retry_count-- > 0) {
            if (task->req->retry_delay > 0) {
                // try again after delay
                setTimeout(task->req->retry_delay, [this, task](TimerID timerID){
                    hlogi("retry %s %s", http_method_str(task->req->method), task->req->url.c_str());
                    sendInLoop(task);
                });
            } else {
                send(task);
            }
        } else {
            ctx->errorCallback();
        }
        removeChannel(channel);
    };

    // timer
    if (timeout_ms > 0) {
        ctx->timerID = setTimeout(timeout_ms - elapsed_ms, [&channel](TimerID timerID){
            HttpClientContext* ctx = channel->getContext<HttpClientContext>();
            assert(ctx->task != NULL);
            hlogw("%s timeout!", ctx->task->req->url.c_str());
            if (channel) {
                channel->close();
            }
        });
    }

    if (channel->isConnected()) {
        // sendRequest
        sendRequest(channel);
    } else {
        // startConnect
        if (req->connect_timeout > 0) {
            channel->setConnectTimeout(req->connect_timeout * 1000);
        }
        channel->startConnect();
    }

    return 0;
}

// InitResponse => SubmitRequest => while(GetSendData) write => startRead
int AsyncHttpClient::sendRequest(const SocketChannelPtr& channel) {
    HttpClientContext* ctx = (HttpClientContext*)channel->context();
    assert(ctx != NULL && ctx->task != NULL);
    HttpRequest* req = ctx->task->req.get();
    HttpResponse* resp = ctx->resp.get();

    if (ctx->parser == NULL) {
        ctx->parser.reset(HttpParser::New(HTTP_CLIENT, (http_version)req->http_major));
    }
    if (resp == NULL) {
        resp = new HttpResponse;
        ctx->resp.reset(resp);
    }
    if (req->http_cb) resp->http_cb = std::move(req->http_cb);

    ctx->parser->InitResponse(resp);
    ctx->parser->SubmitRequest(req);

    char* data = NULL;
    size_t len = 0;
    while (ctx->parser->GetSendData(&data, &len)) {
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
