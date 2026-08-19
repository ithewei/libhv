#include "AsyncRedisClient.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <utility>

namespace hv {

// The AsyncRedisClient IS a TcpClient (see header). Impl keeps a back-pointer to
// that TcpClient (`self`) and drives the RESP protocol through the inherited
// base API: self->channel / self->send / self->isConnected / self->startConnect
// / self->setReconnect. Connect/reconnect/DNS/loop-ownership all live in the
// base, so there is no duplicated tcp_client member and no isLoopOwner logic here.
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
        enum Owner {
            kPending,
            kLoop,
            kCancelled,
        };

        std::mutex mutex;
        std::condition_variable cv;
        std::atomic<int> owner;
        bool done;

        CleanupState()
            : owner(kPending)
            , done(false) {}
    };

    AsyncRedisClient* self;
    std::deque<std::shared_ptr<PendingRequest> > pending;
    RedisParser parser;
    std::string host;
    int port;
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

    explicit Impl(AsyncRedisClient* client)
        : self(client)
        , port(6379)
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
        if (!started || destroyed || stop_in_progress || !accept_requests) {
            return false;
        }
        if (self->loop() == NULL || self->loop()->loop() == NULL) {
            return false;
        }
        // The request is dispatched onto the loop; it can only be served if that
        // loop is actually running (whether the client owns it or the caller
        // supplied and started it).
        return self->loop()->isRunning();
    }

    void initCallbacks() {
        self->onConnection = [this](const SocketChannelPtr& channel) {
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

        self->onMessage = [this](const SocketChannelPtr&, Buffer* buf) {
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

    // Copy the redis target into the inherited TcpClient fields. For a numeric IP
    // (or UDS) resolve the sockaddr up front; for a hostname leave remote_addr
    // zeroed so the base startConnect() runs the non-blocking async DNS path.
    int applySettings() {
        self->remote_host = host.empty() ? "127.0.0.1" : host;
        self->remote_port = port;
        memset(&self->remote_addr, 0, sizeof(self->remote_addr));
        if (self->remote_port < 0 || is_ipaddr(self->remote_host.c_str())) {
            int ret = sockaddr_set_ipport(&self->remote_addr, self->remote_host.c_str(), self->remote_port);
            if (ret != 0) {
                return NABS(ret);
            }
        }
        return 0;
    }

    void clearCallbacks() {
        self->onConnection = NULL;
        self->onMessage = NULL;
        self->onWriteComplete = NULL;
        if (self->channel) {
            self->channel->onconnect = NULL;
            self->channel->onread = NULL;
            self->channel->onwrite = NULL;
            self->channel->onclose = NULL;
        }
    }

    void cleanupInPlace() {
        self->setReconnect(NULL);
        failPending(ERR_CONNECT);
        clearProtocolState();
        clearCallbacks();
        if (self->channel && !self->channel->isClosed()) {
            self->channel->close();
        }
    }

    void runCleanupOnLoopAndWait() {
        if (self->loop()->isInLoopThread()) {
            cleanupInPlace();
            return;
        }
        std::shared_ptr<CleanupState> state = std::make_shared<CleanupState>();
        self->loop()->queueInLoop([this, state]() {
            int expected = CleanupState::kPending;
            if (!state->owner.compare_exchange_strong(expected, CleanupState::kLoop)) {
                return;
            }
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
            int expected = CleanupState::kPending;
            if (state->owner.compare_exchange_strong(expected, CleanupState::kCancelled)) {
                cleanupInPlace();
            } else {
                std::unique_lock<std::mutex> wait_lock(state->mutex);
                state->cv.wait(wait_lock, [state]() { return state->done; });
            }
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
        if (self->isConnected() && !handshake_pending) {
            flushPending();
        }
        else if (started && !handshake_pending) {
            self->startConnect();
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
        int ret = self->send(RedisEncodeCommand(handshake_commands[index]));
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
        if (!self->isConnected()) {
            if (started) {
                self->startConnect();
            }
            return;
        }
        for (size_t i = 0; i < pending.size(); ++i) {
            const std::shared_ptr<PendingRequest>& request = pending[i];
            if (request->sent) {
                continue;
            }
            int ret = self->send(request->payload);
            if (ret < 0) {
                handleClientError(ret);
                return;
            }
            request->sent = true;
        }
    }

    void failPending(int code) {
        while (!pending.empty()) {
            std::shared_ptr<PendingRequest> request = pending.front();
            pending.pop_front();
            cancelTimeout(request);
            invokeRequestCallback(request, code);
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
            self->setReconnect(NULL);
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
        std::shared_ptr<PendingRequest> request = pending.front();
        request->replies.push_back(reply);
        if (request->replies.size() < request->expected_replies) {
            return;
        }
        pending.pop_front();
        cancelTimeout(request);
        invokeRequestCallback(request, 0);
    }

    void handleClientError(int code) {
        notifyError(code);
        failPending(code);
        clearProtocolState();
        if (self->channel && !self->channel->isClosed()) {
            self->channel->close();
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
    : TcpClientTmpl<SocketChannel>(loop)
    , impl_(std::make_shared<Impl>(this)) {
    // preserve the historical redis default connect timeout (base default differs)
    setConnectTimeout(5000);
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

void AsyncRedisClient::setTimeout(int ms) {
    impl_->timeout_ms = ms;
}

void AsyncRedisClient::start(bool wait_threads_started) {
    impl_->destroyed = false;
    impl_->stop_in_progress = false;
    impl_->started = true;
    impl_->accept_requests = true;
    int ret = impl_->applySettings();
    if (ret != 0) {
        impl_->started = false;
        impl_->accept_requests = false;
        impl_->notifyError(ret);
        return;
    }
    // Delegate connect/thread/reconnect to the base. startConnect() runs on the
    // loop and wires channel callbacks into self->onConnection / onMessage.
    TcpClientTmpl<SocketChannel>::start(wait_threads_started);
}

void AsyncRedisClient::stop(bool wait_threads_stopped) {
    impl_->accept_requests = false;
    impl_->started = false;
    impl_->destroyed = true;
    impl_->stop_in_progress = true;
    // Fail any in-flight requests and detach protocol callbacks on the loop
    // thread before the base tears down the socket / loop.
    if (loop() && loop()->loop()) {
        if (loop()->isRunning()) {
            impl_->runCleanupOnLoopAndWait();
        }
        else {
            impl_->cleanupInPlace();
        }
    }
    else {
        impl_->cleanupInPlace();
    }
    TcpClientTmpl<SocketChannel>::stop(wait_threads_stopped);
    impl_->stop_in_progress = false;
}

bool AsyncRedisClient::isConnected() const {
    return channel && channel->isConnected();
}

bool AsyncRedisClient::isStarted() const {
    return impl_->started;
}

bool AsyncRedisClient::isInLoopThread() {
    return loop() && loop()->isInLoopThread();
}

int AsyncRedisClient::command(const RedisCommand& command, RedisCallback cb) {
    if (command.empty()) {
        if (cb) {
            cb(RedisResult(ERR_INVALID_PARAM));
        }
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
        if (cb) {
            cb(ERR_INVALID_PARAM, std::vector<RedisReply>());
        }
        return ERR_INVALID_PARAM;
    }
    auto request = std::make_shared<Impl::PendingRequest>();
    request->expected_replies = commands.size();
    request->batch_callback = std::move(cb);
    for (size_t i = 0; i < commands.size(); ++i) {
        if (commands[i].empty()) {
            if (request->batch_callback) {
                request->batch_callback(ERR_INVALID_PARAM, std::vector<RedisReply>());
            }
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
