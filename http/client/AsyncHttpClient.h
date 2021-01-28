#ifndef HV_ASYNC_HTTP_CLIENT_H_
#define HV_ASYNC_HTTP_CLIENT_H_

#include <list>

#include "EventLoopThread.h"
#include "Channel.h"

#include "HttpMessage.h"
#include "HttpParser.h"

// async => keepalive => connect_pool

namespace hv {

template<typename Conn>
class ConnPool {
public:
    int size() {
        return conns_.size();
    }

    bool get(Conn& conn) {
        if (conns_.empty()) return false;
        conn = conns_.front();
        conns_.pop_front();
        return true;
    }

    bool add(const Conn& conn) {
        conns_.push_back(conn);
        return true;
    }

    bool remove(const Conn& conn) {
        auto iter = conns_.begin();
        while (iter != conns_.end()) {
            if (*iter == conn) {
                iter = conns_.erase(iter);
                return true;
            } else {
                ++iter;
            }
        }
        return false;
    }

private:
    std::list<Conn>  conns_;
};

struct HttpClientTask {
    HttpRequestPtr          req;
    HttpResponseCallback    cb;

    uint64_t  start_time;
    int       retry_cnt;
};
typedef std::shared_ptr<HttpClientTask> HttpClientTaskPtr;

struct HttpClientContext {
    HttpRequestPtr          req;
    HttpResponseCallback    cb;

    SocketChannelPtr    channel;
    HttpResponsePtr     resp;
    HttpParserPtr       parser;

    TimerID             timerID;

    HttpClientContext() {
        timerID = INVALID_TIMER_ID;
    }

    void callback() {
        if (timerID != INVALID_TIMER_ID) {
            killTimer(timerID);
            timerID = INVALID_TIMER_ID;
        }
        if (cb) {
            cb(resp);
            // NOTE: ensure cb just call once
            cb = NULL;
        }
    }

    void successCallback() {
        callback();
        resp = NULL;
    }

    void errorCallback() {
        resp = NULL;
        callback();
    }
};
typedef std::shared_ptr<HttpClientContext>  HttpClientContextPtr;

class AsyncHttpClient {
public:
    AsyncHttpClient() {
        loop_thread.start(true);
    }
    ~AsyncHttpClient() {
        loop_thread.stop(true);
    }

    // thread-safe
    int send(const HttpRequestPtr& req, HttpResponseCallback resp_cb) {
        HttpClientTaskPtr task(new HttpClientTask);
        task->req = req;
        task->cb = resp_cb;
        task->start_time = hloop_now_hrtime(loop_thread.hloop());
        task->retry_cnt = 3;
        return send(task);
    }

    int send(const HttpClientTaskPtr& task) {
        loop_thread.loop()->queueInLoop(std::bind(&AsyncHttpClient::sendInLoop, this, task));
        return 0;
    }

protected:
    void sendInLoop(const HttpClientTaskPtr& task) {
        int err = doTask(task);
        if (err != 0 && task->cb) {
            task->cb(NULL);
        }
    }
    // createsocket => startConnect =>
    // onconnect => sendRequest => startRead =>
    // onread => HttpParser => resp_cb
    int doTask(const HttpClientTaskPtr& task);

    // InitResponse => SubmitRequest => while(GetSendData) write => startRead
    static void onconnect(hio_t* io);
    static int sendRequest(const HttpClientContextPtr ctx);

    HttpClientContextPtr getContext(int fd) {
        return fd < client_ctxs.capacity() ? client_ctxs[fd] : NULL;
    }

    void addContext(const HttpClientContextPtr& ctx) {
        int fd = ctx->channel->fd();
        if (fd >= client_ctxs.capacity()) {
            client_ctxs.resize(2 * fd);
        }
        client_ctxs[fd] = ctx;
        // NOTE: add into conn_pools after recv response completed
        // conn_pools[ctx->channel->peeraddr()].add(fd);
    }

    void removeContext(const HttpClientContextPtr& ctx) {
        int fd = ctx->channel->fd();
        // NOTE: remove from conn_pools
        auto iter = conn_pools.find(ctx->channel->peeraddr());
        if (iter != conn_pools.end()) {
            iter->second.remove(fd);
        }
        if (fd < client_ctxs.capacity()) {
            client_ctxs[fd] = NULL;
        }
    }

private:
    EventLoopThread                         loop_thread;
    // NOTE: just one loop thread, no need mutex.
    // with fd as index
    std::vector<HttpClientContextPtr>       client_ctxs;
    // peeraddr => ConnPool
    std::map<std::string, ConnPool<int>>    conn_pools;
};

}

#endif // HV_ASYNC_HTTP_CLIENT_H_
