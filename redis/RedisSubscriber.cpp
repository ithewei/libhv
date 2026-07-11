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
#include "TcpClient.h"

namespace hv {

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
        std::mutex mutex;
        std::condition_variable cv;
        bool done;

        CleanupState()
            : done(false) {}
    };

    enum OperationType {
        kSubscribe,
        kPSubscribe,
        kUnsubscribe,
        kPUnsubscribe,
    };

    RedisSubscriber* self;
    TcpClientEventLoopTmpl<SocketChannel> tcp_client;
    RedisParser parser;
    bool is_loop_owner;
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

    Impl(RedisSubscriber* subscriber, const EventLoopPtr& loop, bool loop_owner)
        : self(subscriber)
        , tcp_client(loop)
        , is_loop_owner(loop_owner)
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
                clearProtocolState();
                beginHandshake();
                return;
            }
            clearProtocolState();
            if (!stop_in_progress) {
                notifyError(ERR_CONNECT);
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
            if (tcp_client.isConnected() && !handshake_pending) {
                command = RedisCommand{"UNSUBSCRIBE", name};
            }
            break;
        case kPUnsubscribe:
            if (patterns.erase(name) == 0) {
                return 0;
            }
            if (tcp_client.isConnected() && !handshake_pending) {
                command = RedisCommand{"PUNSUBSCRIBE", name};
            }
            break;
        }

        if (!command.empty() && tcp_client.isConnected() && !handshake_pending) {
            return sendCommand(command);
        }
        if (!tcp_client.isConnected() && started && !handshake_pending) {
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
        int ret = sendCommand(handshake_commands[index]);
        if (ret < 0) {
            handleClientError(ret);
            return;
        }
        handshake_index = index;
    }

    int sendCommand(const RedisCommand& command) {
        int ret = tcp_client.send(RedisEncodeCommand(command));
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
        if (tcp_client.channel && !tcp_client.channel->isClosed()) {
            tcp_client.channel->close();
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
    : EventLoopThread(loop)
    , impl_(std::make_shared<Impl>(this, EventLoopThread::loop(), loop == NULL)) {
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

void RedisSubscriber::setReconnect(reconn_setting_t* setting) {
    impl_->tcp_client.setReconnect(setting);
}

void RedisSubscriber::start(bool wait_threads_started) {
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

void RedisSubscriber::stop(bool wait_threads_stopped) {
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
