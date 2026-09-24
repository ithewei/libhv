#ifndef HV_ASYNC_HTTP_CLIENT_H_
#define HV_ASYNC_HTTP_CLIENT_H_

#include <map>
#include <list>
#include <tuple>

#include "EventLoopThread.h"
#include "Channel.h"

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

struct HttpConnKey {
    std::string target_host;
    int target_port;
    std::string proxy_host;
    int proxy_port;
    bool tls;

    HttpConnKey()
        : target_port(0)
        , proxy_port(0)
        , tls(false)
    {}

    explicit HttpConnKey(const HttpRequest& req)
        : HttpConnKey()
    {
        target_host = req.host;
        target_port = req.port;
        proxy_host = req.IsProxy() ? req.proxy_host : "";
        proxy_port = req.IsProxy() ? req.proxy_port : 0;
        tls = req.IsHttps();
    }

    static HttpConnKey Direct(const char* host, int port, bool tls) {
        HttpConnKey key;
        key.target_host = host ? host : "";
        key.target_port = port;
        key.tls = tls;
        return key;
    }

    bool operator==(const HttpConnKey& rhs) const {
        return target_port == rhs.target_port &&
               proxy_port == rhs.proxy_port &&
               tls == rhs.tls &&
               target_host == rhs.target_host &&
               proxy_host == rhs.proxy_host;
    }

    bool operator!=(const HttpConnKey& rhs) const {
        return !(*this == rhs);
    }

    bool operator<(const HttpConnKey& rhs) const {
        return std::tie(target_host, target_port, proxy_host, proxy_port, tls) <
               std::tie(rhs.target_host, rhs.target_port, rhs.proxy_host, rhs.proxy_port, rhs.tls);
    }
};

struct HttpClientContext {
    HttpClientTaskPtr   task;
    HttpConnKey         conn_key;

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
    AsyncHttpClient(EventLoopPtr loop = NULL)
        : EventLoopThread(loop)
    {
        if (loop == NULL) {
            EventLoopThread::start(true);
        }
    }
    ~AsyncHttpClient() {
        EventLoopThread::stop(true);
        // Detach per-connection close callbacks before members are destroyed.
        // Member dtors run in reverse declaration order (conn_pools before
        // channels); tearing down `channels` fires ~Channel -> close -> the
        // onclose lambda, which references conn_pools/channels and would touch
        // an already-destroyed map (UAF). With onclose cleared, Channel::
        // on_close is a no-op. This matters for external (non-owned) loops,
        // where in-flight keep-alive connections outlive send() and are only
        // closed here. Safe now: no loop thread is running concurrently
        // (owned: joined above; external: same-thread teardown).
        for (auto& pair : channels) {
            if (pair.second) pair.second->onclose = NULL;
        }
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

    // Create/configure a new channel, bind its first task and start connecting.
    int startConnect(const HttpClientTaskPtr& task, const sockaddr_u* peeraddr);

    // Bind the current task and arm its remaining end-to-end timeout.
    int startTask(const HttpClientTaskPtr& task, const SocketChannelPtr& channel);

    // Return elapsed milliseconds when task is still runnable, -1 when
    // cancelled and -10 when its end-to-end timeout has expired.
    int checkTaskCancelOrTimeout(const HttpClientTaskPtr& task);

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
    // transport identity => ConnPool
    std::map<HttpConnKey, ConnPool<int>>    conn_pools;
};

}

#endif // HV_ASYNC_HTTP_CLIENT_H_
