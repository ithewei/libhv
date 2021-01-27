#include "AsyncHttpClient.h"

namespace hv {

int AsyncHttpClient::sendInLoopImpl(const HttpRequestPtr& req, HttpResponseCallback resp_cb, uint64_t start_hrtime) {
    // queueInLoop timeout?
    uint64_t now_hrtime = hloop_now_hrtime(loop_thread.hloop());
    int elapsed_ms = (now_hrtime - start_hrtime) / 1000;
    int timeout_ms = req->timeout * 1000;
    if (timeout_ms > 0 && elapsed_ms >= timeout_ms) {
        hlogw("%s queueInLoop timeout!", req->url.c_str());
        return -10;
    }

    req->ParseUrl();
    sockaddr_u peeraddr;
    memset(&peeraddr, 0, sizeof(peeraddr));
    int ret = sockaddr_set_ipport(&peeraddr, req->host.c_str(), req->port);
    if (ret != 0) {
        hloge("unknown host %s", req->host.c_str());
        return -20;
    }

    int connfd = -1;
    hio_t* connio = NULL;
    HttpClientContextPtr ctx = NULL;

    // first get from conn_pools
    char strAddr[SOCKADDR_STRLEN] = {0};
    SOCKADDR_STR(&peeraddr, strAddr);
    auto iter = conn_pools.find(strAddr);
    if (iter != conn_pools.end()) {
        if (iter->second.get(connfd)) {
            // hlogd("get from conn_pools");
            ctx = getContext(connfd);
            ctx->req = req;
            ctx->cb = resp_cb;
        }
    }

    if (connfd < 0) {
        // create socket
        connfd = socket(peeraddr.sa.sa_family, SOCK_STREAM, 0);
        if (connfd < 0) {
            perror("socket");
            return -30;
        }
        connio = hio_get(loop_thread.hloop(), connfd);
        assert(connio != NULL);
        hio_set_peeraddr(connio, &peeraddr.sa, sockaddr_len(&peeraddr));
        // https
        if (req->https) {
            hio_enable_ssl(connio);
        }
    }

    if (ctx == NULL) {
        // new HttpClientContext
        ctx.reset(new HttpClientContext);
        ctx->req = req;
        ctx->cb = resp_cb;
        ctx->channel.reset(new SocketChannel(connio));
        ctx->channel->onread = [this, ctx](Buffer* buf) {
            const char* data = (const char*)buf->data();
            int len = buf->size();
            int nparse = ctx->parser->FeedRecvData(data, len);
            if (nparse != len) {
                ctx->errorCallback();
                ctx->channel->close();
                return;
            }
            if (ctx->parser->IsComplete()) {
                std::string req_connection = ctx->req->GetHeader("Connection");
                std::string resp_connection = ctx->resp->GetHeader("Connection");
                ctx->successCallback();
                if (stricmp(req_connection.c_str(), "keep-alive") == 0 &&
                    stricmp(resp_connection.c_str(), "keep-alive") == 0) {
                    // add into conn_pools to reuse
                    // hlogd("add into conn_pools");
                    conn_pools[ctx->channel->peeraddr()].add(ctx->channel->fd());
                } else {
                    ctx->channel->close();
                }
            }
        };
        ctx->channel->onclose = [this, ctx]() {
            ctx->channel->status = SocketChannel::CLOSED;
            removeContext(ctx);
            ctx->errorCallback();
        };
        addContext(ctx);
    }

    // timer
    if (timeout_ms > 0) {
        ctx->timerID = setTimeout(timeout_ms - elapsed_ms, [ctx](TimerID timerID){
            hlogw("%s timeout!", ctx->req->url.c_str());
            if (ctx->channel) {
                ctx->channel->close();
            }
        });
    }

    if (ctx->channel->isConnected()) {
        // sendRequest
        sendRequest(ctx);
    } else {
        // startConnect
        hevent_set_userdata(connio, this);
        hio_setcb_connect(connio, onconnect);
        hio_connect(connio);
    }

    return 0;
}

void AsyncHttpClient::onconnect(hio_t* io) {
    AsyncHttpClient* client = (AsyncHttpClient*)hevent_userdata(io);
    HttpClientContextPtr ctx = client->getContext(hio_fd(io));
    assert(ctx != NULL && ctx->req != NULL && ctx->channel != NULL);

    ctx->channel->status = SocketChannel::CONNECTED;
    client->sendRequest(ctx);
    ctx->channel->startRead();
}

int AsyncHttpClient::sendRequest(const HttpClientContextPtr ctx) {
    assert(ctx != NULL && ctx->req != NULL && ctx->channel != NULL);
    SocketChannelPtr channel = ctx->channel;

    if (ctx->parser == NULL) {
        ctx->parser.reset(HttpParser::New(HTTP_CLIENT, (http_version)ctx->req->http_major));
    }
    if (ctx->resp == NULL) {
        ctx->resp.reset(new HttpResponse);
    }

    ctx->parser->InitResponse(ctx->resp.get());
    ctx->parser->SubmitRequest(ctx->req.get());

    char* data = NULL;
    size_t len = 0;
    while (ctx->parser->GetSendData(&data, &len)) {
        Buffer buf(data, len);
        channel->write(&buf);
    }

    return 0;
}

}
