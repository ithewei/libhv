#ifndef HV_ASYNC_HTTP_CLIENT_H_
#define HV_ASYNC_HTTP_CLIENT_H_

#include <map>
#include <list>
#include <set>

#include "EventLoopThread.h"
#include "Channel.h"
#include "hdns.h"

#include "HttpMessage.h"
#include "HttpParser.h"

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
    uint64_t                start_time;
};
typedef std::shared_ptr<HttpClientTask> HttpClientTaskPtr;

struct HttpClientContext {
    HttpClientTaskPtr   task;

    HttpResponsePtr     resp;
    HttpParserPtr       parser;
    TimerID             timerID;

    HttpClientContext() {
        timerID = INVALID_TIMER_ID;
    }

    ~HttpClientContext() {
        cancelTimer();
    }

    void cancelTimer() {
        if (timerID != INVALID_TIMER_ID) {
            killTimer(timerID);
            timerID = INVALID_TIMER_ID;
        }
    }

    void cancelTask() {
        cancelTimer();
        task = NULL;
    }

    void callback() {
        cancelTimer();
        if (task && task->cb) {
            task->cb(resp);
        }
        // NOTE: task done
        task = NULL;
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

class HV_EXPORT AsyncHttpClient : private EventLoopThread {
public:
    AsyncHttpClient(EventLoopPtr loop = NULL) : EventLoopThread(loop) {
        if (loop == NULL) {
            EventLoopThread::start(true);
        }
    }
    ~AsyncHttpClient() {
        // Stop (and, for an owned loop, free) the event loop first. An owned
        // loop uses HLOOP_FLAG_AUTO_FREE, so joining the loop thread runs
        // hloop_free() -> hdns_resolver_free(), which cancels and frees every
        // in-flight hdns_query_t. After stop() the loop thread is joined, so no
        // resolve callback can race with teardown.
        EventLoopThread::stop(true);
        // The resolver already freed the hdns_query_t objects during loop
        // teardown, so DO NOT call hdns_cancel() here (that would be a
        // use-after-free). Just free our own per-request context shells.
        for (auto* dctx : dns_queries) {
            delete dctx;
        }
        dns_queries.clear();
    }

    // thread-safe
    int send(const HttpRequestPtr& req, HttpResponseCallback resp_cb);
    int send(const HttpClientTaskPtr& task) {
        EventLoopThread::loop()->queueInLoop(std::bind(&AsyncHttpClient::sendInLoop, this, task));
        return 0;
    }

protected:
    void sendInLoop(HttpClientTaskPtr task) {
        int err = doTask(task);
        if (err != 0 && task->cb) {
            task->cb(NULL);
        }
    }
    int doTask(const HttpClientTaskPtr& task);

    // async DNS resolve context for one task (heap-allocated, tracked so it can
    // be cancelled/freed on teardown).
    struct DnsContext {
        AsyncHttpClient*    client;
        HttpClientTaskPtr   task;
        hdns_query_t*       query;
    };
    // @internal: continue doTask after the peer address is known.
    int doTaskWithAddr(const HttpClientTaskPtr& task, const sockaddr_u* peeraddr);
    static void onDnsResolved(const hdns_result_t* result, void* userdata);

    static int sendRequest(const SocketChannelPtr& channel);

    // channel
    const SocketChannelPtr& getChannel(int fd) {
        return channels[fd];
        // return fd < channels.capacity() ? channels[fd] : NULL;
    }

    const SocketChannelPtr& addChannel(hio_t* io) {
        auto channel = std::make_shared<SocketChannel>(io);
        channel->newContext<HttpClientContext>();
        int fd = channel->fd();
        channels[fd] = channel;
        return channels[fd];
    }

    void removeChannel(const SocketChannelPtr& channel) {
        channel->deleteContext<HttpClientContext>();
        int fd = channel->fd();
        channels.erase(fd);
    }

private:
    // NOTE: just one loop thread, no need mutex.
    // fd => SocketChannelPtr
    std::map<int, SocketChannelPtr>         channels;
    // peeraddr => ConnPool
    std::map<std::string, ConnPool<int>>    conn_pools;
    // in-flight async DNS resolve contexts (for cancel/free on teardown)
    std::set<DnsContext*>                   dns_queries;
};

}

#endif // HV_ASYNC_HTTP_CLIENT_H_
