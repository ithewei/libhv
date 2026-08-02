#include "RedisSubscriber.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

#include "RedisMessage.h"

namespace hv {

// The RedisSubscriber IS a TcpClient (see header). Impl keeps a back-pointer to
// that TcpClient (`self`) and drives the RESP subscribe protocol through the
// inherited base API: self->channel / self->send / self->isConnected /
// self->startConnect / self->setReconnect. Connect/reconnect/DNS/loop-ownership
// all live in the base, so there is no duplicated tcp_client member here.
struct RedisSubscriber::Impl {
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

    enum OperationType {
        kSubscribe,
        kPSubscribe,
        kUnsubscribe,
        kPUnsubscribe,
    };

    RedisSubscriber* self;
    RedisParser parser;
    std::string host;
    int port;
    std::string password;
    int db;
    bool handshake_pending;
    std::atomic<bool> started;
    std::atomic<bool> accept_requests;
    std::atomic<bool> destroyed;
    std::atomic<bool> stop_in_progress;
    size_t handshake_index;
    std::vector<RedisCommand> handshake_commands;
    std::set<std::string> channels;
    std::set<std::string> patterns;

    explicit Impl(RedisSubscriber* subscriber)
        : self(subscriber)
        , port(6379)
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
        return self->loop()->isRunning();
    }

    void initCallbacks() {
        // Wire the base (TcpClient) transport callbacks. RedisSubscriber also
        // declares a public onMessage(channel, message) for pub/sub delivery,
        // which HIDES the base onMessage(channelPtr, Buffer*); route through a
        // base-typed pointer so we set the transport callback, not the
        // user-facing one.
        TcpClientEventLoopTmpl<SocketChannel>* tcp = self;
        tcp->onConnection = [this](const SocketChannelPtr& channel) {
            if (destroyed) {
                return;
            }
            if (channel->isConnected()) {
                clearProtocolState();
                beginHandshake();
                return;
            }
            clearProtocolState();
            if (!stop_in_progress) {
                notifyError(ERR_CONNECT);
            }
        };

        tcp->onMessage = [this](const SocketChannelPtr&, Buffer* buf) {
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
        // Clear the base transport callbacks via a base-typed pointer (see
        // initCallbacks): self->onMessage would resolve to the hiding pub/sub
        // callback, not the base one.
        TcpClientEventLoopTmpl<SocketChannel>* tcp = self;
        tcp->onConnection = NULL;
        tcp->onMessage = NULL;
        tcp->onWriteComplete = NULL;
        if (self->channel) {
            self->channel->onconnect = NULL;
            self->channel->onread = NULL;
            self->channel->onwrite = NULL;
            self->channel->onclose = NULL;
        }
    }

    void cleanupInPlace() {
        self->setReconnect(NULL);
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

    int queueOperation(OperationType type, const std::string& name) {
        if (name.empty()) {
            return ERR_INVALID_PARAM;
        }
        if (!acceptsRequests()) {
            return ERR_CONNECT;
        }
        if (self->loop()->isInLoopThread()) {
            return performOperation(type, name, NULL);
        }
        std::shared_ptr<EnqueueState> state = std::make_shared<EnqueueState>();
        self->loop()->queueInLoop([this, type, name, state]() {
            int code = performOperation(type, name, state);
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

    int performOperation(OperationType type, const std::string& name, const std::shared_ptr<EnqueueState>& state) {
        if (state) {
            int expected = EnqueueState::kPending;
            if (!state->owner.compare_exchange_strong(expected, EnqueueState::kLoop)) {
                return ERR_CONNECT;
            }
        }
        if (!acceptsRequests()) {
            return ERR_CONNECT;
        }

        RedisCommand command;
        switch (type) {
        case kSubscribe:
            if (!channels.insert(name).second) {
                return 0;
            }
            command = RedisCommand{"SUBSCRIBE", name};
            break;
        case kPSubscribe:
            if (!patterns.insert(name).second) {
                return 0;
            }
            command = RedisCommand{"PSUBSCRIBE", name};
            break;
        case kUnsubscribe:
            if (channels.erase(name) == 0) {
                return 0;
            }
            if (self->isConnected() && !handshake_pending) {
                command = RedisCommand{"UNSUBSCRIBE", name};
            }
            break;
        case kPUnsubscribe:
            if (patterns.erase(name) == 0) {
                return 0;
            }
            if (self->isConnected() && !handshake_pending) {
                command = RedisCommand{"PUNSUBSCRIBE", name};
            }
            break;
        }

        if (!command.empty() && self->isConnected() && !handshake_pending) {
            return sendCommand(command);
        }
        if (!self->isConnected() && started && !handshake_pending) {
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
        int ret = sendCommand(handshake_commands[index]);
        if (ret < 0) {
            handleClientError(ret);
            return;
        }
        handshake_index = index;
    }

    int sendCommand(const RedisCommand& command) {
        int ret = self->send(RedisEncodeCommand(command));
        if (ret < 0) {
            handleClientError(ret);
            return ret;
        }
        return 0;
    }

    int sendSubscribeSet(const std::set<std::string>& values, const char* verb) {
        for (std::set<std::string>::const_iterator it = values.begin(); it != values.end(); ++it) {
            int ret = sendCommand(RedisCommand{verb, *it});
            if (ret < 0) {
                return ret;
            }
        }
        return 0;
    }

    int syncSubscriptions() {
        int ret = sendSubscribeSet(channels, "SUBSCRIBE");
        if (ret < 0) {
            return ret;
        }
        return sendSubscribeSet(patterns, "PSUBSCRIBE");
    }

    void handleHandshakeReply(const RedisReply& reply) {
        if (reply.isError()) {
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

    static const RedisReply* arrayElement(const RedisReply& reply, size_t index) {
        if (!reply.isArray() || reply.elements.size() <= index) {
            return NULL;
        }
        return &reply.elements[index];
    }

    static bool arrayString(const RedisReply& reply, size_t index, std::string* out) {
        const RedisReply* element = arrayElement(reply, index);
        if (element == NULL || !element->isString()) {
            return false;
        }
        if (out) {
            *out = element->asString();
        }
        return true;
    }

    void handleReply(const RedisReply& reply) {
        if (handshake_pending) {
            handleHandshakeReply(reply);
            return;
        }
        if (!reply.isArray()) {
            handleClientError(ERR_RESPONSE);
            return;
        }

        std::string kind;
        if (!arrayString(reply, 0, &kind)) {
            handleClientError(ERR_RESPONSE);
            return;
        }

        if (kind == "message") {
            std::string channel;
            std::string message;
            if (!arrayString(reply, 1, &channel) || !arrayString(reply, 2, &message)) {
                handleClientError(ERR_RESPONSE);
                return;
            }
            if (self->onMessage) {
                self->onMessage(channel, message);
            }
            return;
        }

        if (kind == "pmessage") {
            std::string channel;
            std::string message;
            if (!arrayString(reply, 2, &channel) || !arrayString(reply, 3, &message)) {
                handleClientError(ERR_RESPONSE);
                return;
            }
            if (self->onMessage) {
                self->onMessage(channel, message);
            }
            return;
        }

        if (kind == "subscribe" || kind == "psubscribe") {
            std::string name;
            if (!arrayString(reply, 1, &name)) {
                handleClientError(ERR_RESPONSE);
                return;
            }
            if (self->onSubscribe) {
                self->onSubscribe(name);
            }
            return;
        }

        if (kind == "unsubscribe" || kind == "punsubscribe") {
            std::string name;
            if (!arrayString(reply, 1, &name)) {
                handleClientError(ERR_RESPONSE);
                return;
            }
            if (self->onUnsubscribe) {
                self->onUnsubscribe(name);
            }
            return;
        }

        handleClientError(ERR_RESPONSE);
    }

    void handleClientError(int code) {
        notifyError(code);
        clearProtocolState();
        if (self->channel && !self->channel->isClosed()) {
            self->channel->close();
        }
    }

    void finishHandshake() {
        clearProtocolState();
        if (syncSubscriptions() < 0) {
            return;
        }
    }

    void notifyError(int code) {
        if (self->onError) {
            self->onError(code);
        }
    }
};

RedisSubscriber::RedisSubscriber(EventLoopPtr loop)
    : TcpClientTmpl<SocketChannel>(loop)
    , impl_(std::make_shared<Impl>(this)) {
    impl_->initCallbacks();
}

RedisSubscriber::~RedisSubscriber() {
    stop(true);
}

void RedisSubscriber::setHost(const std::string& host) {
    impl_->host = host;
}

void RedisSubscriber::setPort(int port) {
    impl_->port = port;
}

void RedisSubscriber::setAuth(const std::string& password) {
    impl_->password = password;
}

void RedisSubscriber::setDb(int db) {
    impl_->db = db;
}

void RedisSubscriber::start(bool wait_threads_started) {
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
    TcpClientTmpl<SocketChannel>::start(wait_threads_started);
}

void RedisSubscriber::stop(bool wait_threads_stopped) {
    impl_->accept_requests = false;
    impl_->started = false;
    impl_->destroyed = true;
    impl_->stop_in_progress = true;
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

int RedisSubscriber::subscribe(const std::string& channel) {
    return impl_->queueOperation(Impl::kSubscribe, channel);
}

int RedisSubscriber::psubscribe(const std::string& pattern) {
    return impl_->queueOperation(Impl::kPSubscribe, pattern);
}

int RedisSubscriber::unsubscribe(const std::string& channel) {
    return impl_->queueOperation(Impl::kUnsubscribe, channel);
}

int RedisSubscriber::punsubscribe(const std::string& pattern) {
    return impl_->queueOperation(Impl::kPUnsubscribe, pattern);
}

} // namespace hv
