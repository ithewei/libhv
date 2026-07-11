#include "AsyncRedisClient.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <utility>

#include "TcpClient.h"

namespace hv {

struct AsyncRedisClient::Impl {
    struct PendingRequest {
        size_t expected_replies;
        RedisCallback callback;
        RedisRepliesCallback batch_callback;
        std::vector<RedisReply> replies;
        TimerID timer_id;
        std::string payload;
        bool sent;

        PendingRequest()
            : expected_replies(1)
            , timer_id(INVALID_TIMER_ID)
            , sent(false) {}
    };

    struct EnqueueState {
        enum Owner {
            kPending,
            kLoop,
            kCancelled,
        };

        std::mutex mutex;
        std::condition_variable cv;
        std::atomic<int> owner;
        bool done;
        int code;

        EnqueueState()
            : owner(kPending)
            , done(false)
            , code(ERR_CONNECT) {}
    };

    struct CleanupState {
        std::mutex mutex;
        std::condition_variable cv;
        bool done;

        CleanupState()
            : done(false) {}
    };

    AsyncRedisClient* self;
    TcpClientEventLoopTmpl<SocketChannel> tcp_client;
    std::deque<std::shared_ptr<PendingRequest> > pending;
    RedisParser parser;
    bool is_loop_owner;
    std::string host;
    int port;
    int connect_timeout_ms;
    int timeout_ms;
    std::string password;
    int db;
    bool handshake_pending;
    std::atomic<bool> started;
    std::atomic<bool> accept_requests;
    std::atomic<bool> destroyed;
    std::atomic<bool> stop_in_progress;
    size_t handshake_index;
    std::vector<RedisCommand> handshake_commands;

    Impl(AsyncRedisClient* client, const EventLoopPtr& loop, bool loop_owner)
        : self(client)
        , tcp_client(loop)
        , is_loop_owner(loop_owner)
        , port(6379)
        , connect_timeout_ms(5000)
        , timeout_ms(5000)
        , db(0)
        , handshake_pending(false)
        , started(false)
        , accept_requests(true)
        , destroyed(false)
        , stop_in_progress(false)
        , handshake_index(0) {}

    static bool tryCancelEnqueue(const std::shared_ptr<EnqueueState>& state) {
        int expected = EnqueueState::kPending;
        return state->owner.compare_exchange_strong(expected, EnqueueState::kCancelled);
    }

    bool acceptsRequests() {
        if (!started || destroyed || stop_in_progress || !accept_requests || self->loop() == NULL || self->loop()->loop() == NULL) {
            return false;
        }
        if (!is_loop_owner && !self->loop()->isRunning()) {
            return false;
        }
        return true;
    }

    void initCallbacks() {
        tcp_client.onConnection = [this](const SocketChannelPtr& channel) {
            if (destroyed) {
                return;
            }
            if (channel->isConnected()) {
                if (timeout_ms > 0) {
                    channel->setReadTimeout(timeout_ms);
                    channel->setWriteTimeout(timeout_ms);
                }
                clearProtocolState();
                beginHandshake();
                return;
            }
            clearProtocolState();
            failPending(ERR_CONNECT);
            if (self->onClose) {
                self->onClose();
            }
        };

        tcp_client.onMessage = [this](const SocketChannelPtr&, Buffer* buf) {
            if (destroyed) {
                return;
            }
            parser.Feed((const char*)buf->data(), buf->size());
            if (parser.HasError()) {
                handleClientError(ERR_INVALID_PROTOCOL);
                return;
            }
            while (parser.HasReply()) {
                handleReply(parser.NextReply());
            }
        };
    }

    int applySettings() {
        tcp_client.remote_host = host.empty() ? "127.0.0.1" : host;
        tcp_client.remote_port = port;
        tcp_client.connect_timeout = connect_timeout_ms;
        memset(&tcp_client.remote_addr, 0, sizeof(tcp_client.remote_addr));
        int ret = sockaddr_set_ipport(&tcp_client.remote_addr, tcp_client.remote_host.c_str(), tcp_client.remote_port);
        if (ret != 0) {
            return NABS(ret);
        }
        return 0;
    }

    int startConnectInLoop() {
        if (!accept_requests || destroyed || self->loop() == NULL || self->loop()->loop() == NULL) {
            return ERR_CONNECT;
        }
        if (!is_loop_owner && !self->loop()->isRunning()) {
            return ERR_CONNECT;
        }
        int ret = applySettings();
        if (ret != 0) {
            notifyError(ret);
            return ret;
        }
        ret = tcp_client.startConnect();
        if (ret != 0) {
            notifyError(ret);
        }
        return ret;
    }

    void clearCallbacks() {
        tcp_client.onConnection = NULL;
        tcp_client.onMessage = NULL;
        tcp_client.onWriteComplete = NULL;
        if (tcp_client.channel) {
            tcp_client.channel->onconnect = NULL;
            tcp_client.channel->onread = NULL;
            tcp_client.channel->onwrite = NULL;
            tcp_client.channel->onclose = NULL;
        }
    }

    void cleanupInPlace() {
        tcp_client.setReconnect(NULL);
        failPending(ERR_CONNECT);
        clearProtocolState();
        clearCallbacks();
        if (tcp_client.channel && !tcp_client.channel->isClosed()) {
            tcp_client.channel->close();
        }
    }

    void runCleanupOnLoopAndWait() {
        if (self->loop()->isInLoopThread()) {
            cleanupInPlace();
            return;
        }
        std::shared_ptr<CleanupState> state = std::make_shared<CleanupState>();
        self->loop()->queueInLoop([this, state]() {
            cleanupInPlace();
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->done = true;
            }
            state->cv.notify_one();
        });
        std::unique_lock<std::mutex> lock(state->mutex);
        while (!state->done) {
            if (state->cv.wait_for(lock, std::chrono::milliseconds(10), [state]() { return state->done; })) {
                break;
            }
            if (!self->loop()->isRunning()) {
                break;
            }
        }
        lock.unlock();
        if (!state->done) {
            cleanupInPlace();
        }
    }

    void clearProtocolState() {
        parser.Reset();
        handshake_pending = false;
        handshake_index = 0;
        handshake_commands.clear();
    }

    int enqueueRequest(const std::shared_ptr<PendingRequest>& request) {
        if (!acceptsRequests()) {
            return ERR_CONNECT;
        }
        if (self->loop()->isInLoopThread()) {
            return enqueueRequestInLoop(request, NULL);
        }
        std::shared_ptr<EnqueueState> state = std::make_shared<EnqueueState>();
        self->loop()->queueInLoop([this, request, state]() {
            int code = enqueueRequestInLoop(request, state);
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->code = code;
                state->done = true;
            }
            state->cv.notify_one();
        });
        std::unique_lock<std::mutex> lock(state->mutex);
        while (!state->done) {
            if (state->cv.wait_for(lock, std::chrono::milliseconds(10), [state]() { return state->done; })) {
                break;
            }
            if (!acceptsRequests()) {
                if (tryCancelEnqueue(state)) {
                    break;
                }
            }
        }
        if (!state->done) {
            return ERR_CONNECT;
        }
        return state->code;
    }

    int enqueueRequestInLoop(const std::shared_ptr<PendingRequest>& request, const std::shared_ptr<EnqueueState>& state) {
        if (state) {
            int expected = EnqueueState::kPending;
            if (!state->owner.compare_exchange_strong(expected, EnqueueState::kLoop)) {
                return ERR_CONNECT;
            }
        }
        if (!acceptsRequests()) {
            return ERR_CONNECT;
        }
        pending.push_back(request);
        armTimeout(request);
        if (tcp_client.isConnected() && !handshake_pending) {
            flushPending();
        }
        else if (started && !handshake_pending) {
            tcp_client.start();
        }
        return 0;
    }

    void beginHandshake() {
        handshake_commands.clear();
        if (!password.empty()) {
            handshake_commands.push_back(RedisCommand{"AUTH", password});
        }
        if (db > 0) {
            handshake_commands.push_back(RedisCommand{"SELECT", std::to_string(db)});
        }
        handshake_pending = !handshake_commands.empty();
        if (!handshake_pending) {
            finishHandshake();
            return;
        }
        sendHandshakeCommand(0);
    }

    void sendHandshakeCommand(size_t index) {
        if (index >= handshake_commands.size()) {
            finishHandshake();
            return;
        }
        int ret = tcp_client.send(RedisEncodeCommand(handshake_commands[index]));
        if (ret < 0) {
            handleClientError(ret);
            return;
        }
        handshake_index = index;
    }

    void armTimeout(const std::shared_ptr<PendingRequest>& request) {
        if (timeout_ms <= 0 || !self->loop()) {
            return;
        }
        request->timer_id = self->loop()->setTimeout(timeout_ms, [this, request](TimerID timerID) {
            if (request->timer_id != timerID) {
                return;
            }
            handleClientError(ERR_TASK_TIMEOUT);
        });
    }

    void flushPending() {
        if (!tcp_client.isConnected()) {
            if (started) {
                tcp_client.start();
            }
            return;
        }
        for (size_t i = 0; i < pending.size(); ++i) {
            const std::shared_ptr<PendingRequest>& request = pending[i];
            if (request->sent) {
                continue;
            }
            int ret = tcp_client.send(request->payload);
            if (ret < 0) {
                handleClientError(ret);
                return;
            }
            request->sent = true;
        }
    }

    void failPending(int code) {
        while (!pending.empty()) {
            const std::shared_ptr<PendingRequest>& request = pending.front();
            cancelTimeout(request);
            invokeRequestCallback(request, code);
            pending.pop_front();
        }
    }

    void cancelTimeout(const std::shared_ptr<PendingRequest>& request) {
        if (request->timer_id != INVALID_TIMER_ID && self->loop()) {
            self->loop()->killTimer(request->timer_id);
            request->timer_id = INVALID_TIMER_ID;
        }
    }

    void invokeRequestCallback(const std::shared_ptr<PendingRequest>& request, int code) {
        if (request->callback) {
            RedisResult result(code);
            if (code == 0 && !request->replies.empty()) {
                result.reply = request->replies.front();
            }
            request->callback(result);
        }
        if (request->batch_callback) {
            std::vector<RedisReply> replies;
            if (code == 0) {
                replies = request->replies;
            }
            request->batch_callback(code, replies);
        }
    }

    void handleHandshakeReply(const RedisReply& reply) {
        if (reply.isError()) {
            // Authentication / SELECT rejection is fatal: the password or db is
            // wrong and reconnecting with the same settings only produces an
            // endless reconnect + re-auth storm (TcpClient resets its retry
            // counter on every successful TCP connect). Disable reconnect before
            // closing so the failure is reported once and the client stays down.
            tcp_client.setReconnect(NULL);
            handleClientError(ERR_RESPONSE);
            return;
        }
        size_t next = handshake_index + 1;
        if (next >= handshake_commands.size()) {
            finishHandshake();
            return;
        }
        sendHandshakeCommand(next);
    }

    void handleReply(const RedisReply& reply) {
        if (handshake_pending) {
            handleHandshakeReply(reply);
            return;
        }
        if (pending.empty()) {
            handleClientError(ERR_RESPONSE);
            return;
        }
        const std::shared_ptr<PendingRequest>& request = pending.front();
        request->replies.push_back(reply);
        if (request->replies.size() < request->expected_replies) {
            return;
        }
        cancelTimeout(request);
        invokeRequestCallback(request, 0);
        pending.pop_front();
    }

    void handleClientError(int code) {
        notifyError(code);
        failPending(code);
        clearProtocolState();
        if (tcp_client.channel && !tcp_client.channel->isClosed()) {
            tcp_client.channel->close();
        }
    }

    void finishHandshake() {
        clearProtocolState();
        if (self->onConnect) {
            self->onConnect();
        }
        flushPending();
    }

    void notifyError(int code) {
        if (self->onError) {
            self->onError(code);
        }
    }
};

AsyncRedisClient::AsyncRedisClient(EventLoopPtr loop)
    : EventLoopThread(loop)
    , impl_(std::make_shared<Impl>(this, EventLoopThread::loop(), loop == NULL)) {
    impl_->initCallbacks();
}

AsyncRedisClient::~AsyncRedisClient() {
    stop(true);
}

void AsyncRedisClient::setHost(const std::string& host) {
    impl_->host = host;
}

void AsyncRedisClient::setPort(int port) {
    impl_->port = port;
}

void AsyncRedisClient::setAuth(const std::string& password) {
    impl_->password = password;
}

void AsyncRedisClient::setDb(int db) {
    impl_->db = db;
}

void AsyncRedisClient::setConnectTimeout(int ms) {
    impl_->connect_timeout_ms = ms;
}

void AsyncRedisClient::setTimeout(int ms) {
    impl_->timeout_ms = ms;
}

void AsyncRedisClient::setReconnect(reconn_setting_t* setting) {
    impl_->tcp_client.setReconnect(setting);
}

void AsyncRedisClient::start(bool wait_threads_started) {
    impl_->destroyed = false;
    impl_->stop_in_progress = false;
    impl_->started = true;
    impl_->accept_requests = true;
    if (!impl_->is_loop_owner) {
        if (!loop() || !loop()->loop() || !loop()->isRunning()) {
            impl_->started = false;
            impl_->accept_requests = false;
            impl_->notifyError(ERR_CONNECT);
            return;
        }
        loop()->runInLoop([this]() {
            impl_->startConnectInLoop();
        });
        return;
    }
    if (isRunning()) {
        loop()->runInLoop([this]() {
            impl_->startConnectInLoop();
        });
        return;
    }
    EventLoopThread::start(wait_threads_started, [this]() {
        return impl_->startConnectInLoop();
    });
}

void AsyncRedisClient::stop(bool wait_threads_stopped) {
    impl_->accept_requests = false;
    impl_->started = false;
    impl_->destroyed = true;
    impl_->stop_in_progress = true;
    if (!loop()) {
        impl_->cleanupInPlace();
        impl_->stop_in_progress = false;
        return;
    }
    if (!impl_->is_loop_owner) {
        if (loop()->isRunning()) {
            impl_->runCleanupOnLoopAndWait();
        }
        else {
            impl_->cleanupInPlace();
        }
        impl_->stop_in_progress = false;
        return;
    }
    if (loop()->isRunning()) {
        impl_->runCleanupOnLoopAndWait();
    }
    else {
        impl_->cleanupInPlace();
    }
    EventLoopThread::stop(wait_threads_stopped);
    impl_->stop_in_progress = false;
}

bool AsyncRedisClient::isConnected() const {
    return impl_->tcp_client.channel && impl_->tcp_client.channel->isConnected();
}

bool AsyncRedisClient::isStarted() const {
    return impl_->started;
}

bool AsyncRedisClient::isInLoopThread() {
    return loop() && loop()->isInLoopThread();
}

int AsyncRedisClient::command(const RedisCommand& command, RedisCallback cb) {
    if (command.empty()) {
        return ERR_INVALID_PARAM;
    }
    auto request = std::make_shared<Impl::PendingRequest>();
    request->payload = RedisEncodeCommand(command);
    request->callback = std::move(cb);
    int ret = impl_->enqueueRequest(request);
    if (ret != 0) {
        impl_->invokeRequestCallback(request, ret);
    }
    return ret;
}

int AsyncRedisClient::commandBatch(const std::vector<RedisCommand>& commands, RedisRepliesCallback cb) {
    if (commands.empty()) {
        return ERR_INVALID_PARAM;
    }
    auto request = std::make_shared<Impl::PendingRequest>();
    request->expected_replies = commands.size();
    request->batch_callback = std::move(cb);
    for (size_t i = 0; i < commands.size(); ++i) {
        if (commands[i].empty()) {
            return ERR_INVALID_PARAM;
        }
        request->payload += RedisEncodeCommand(commands[i]);
    }
    int ret = impl_->enqueueRequest(request);
    if (ret != 0) {
        impl_->invokeRequestCallback(request, ret);
    }
    return ret;
}

} // namespace hv
