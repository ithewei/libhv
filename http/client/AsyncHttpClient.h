#ifndef HV_ASYNC_HTTP_CLIENT_H_
#define HV_ASYNC_HTTP_CLIENT_H_

#include <list>

#include "EventLoopThread.h"
#include "Channel.h"

#include "HttpMessage.h"
#include "HttpParser.h"

#define DEFAULT_FAIL_RETRY_COUNT  3
#define DEFAULT_FAIL_RETRY_DELAY  1000  // ms

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

    uint64_t    start_time;
    int         retry_cnt;
    int         retry_delay;
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
        if (timerID != INVALID_TIMER_ID) {
            killTimer(timerID);
            timerID = INVALID_TIMER_ID;
        }
    }

    void callback() {
        if (timerID != INVALID_TIMER_ID) {
            killTimer(timerID);
            timerID = INVALID_TIMER_ID;
        }
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
        task->cb = std::move(resp_cb);
        task->start_time = hloop_now_hrtime(loop_thread.hloop());
        task->retry_cnt = DEFAULT_FAIL_RETRY_COUNT;
        task->retry_delay = DEFAULT_FAIL_RETRY_DELAY;
        return send(task);
    }

    int send(const HttpClientTaskPtr& task) {
        loop_thread.loop()->queueInLoop(std::bind(&AsyncHttpClient::sendInLoop, this, task));
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

    static int sendRequest(const SocketChannelPtr& channel);

    // channel
    const SocketChannelPtr& getChannel(int fd) {
        return channels[fd];
        // return fd < channels.capacity() ? channels[fd] : NULL;
    }

    const SocketChannelPtr& addChannel(hio_t* io) {
        SocketChannelPtr channel(new SocketChannel(io));
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
    EventLoopThread                         loop_thread;
    // NOTE: just one loop thread, no need mutex.
    // fd => SocketChannelPtr
    std::map<int, SocketChannelPtr>         channels;
    // peeraddr => ConnPool
    std::map<std::string, ConnPool<int>>    conn_pools;
};

}

#endif // HV_ASYNC_HTTP_CLIENT_H_
