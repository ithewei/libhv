#include "redis_test_server.h"

#include <cstring>
#include <set>
#include <string>

#include "TcpServer.h"
#include "hbase.h"
#include "hsocket.h"

struct FakeRedisServer::Impl {
    struct ClientState {
        hv::RedisParser parser;
        std::set<std::string> channels;
        std::set<std::string> patterns;
    };

    int port;
    bool close_after_reply;
    std::function<hv::RedisReply(const hv::RedisCommand&)> handler;
    std::shared_ptr<hv::TcpServer> server;

    Impl()
        : port(0)
        , close_after_reply(false) {}

    static std::shared_ptr<ClientState> getOrCreateState(const hv::SocketChannelPtr& channel) {
        std::shared_ptr<ClientState> state = channel->getContextPtr<ClientState>();
        if (!state) {
            state = channel->newContextPtr<ClientState>();
        }
        return state;
    }

    static hv::RedisReply makeStringReply(const std::string& value, bool bulk = false) {
        hv::RedisReply reply;
        reply.type = hv::REDIS_REPLY_STRING;
        reply.str = value;
        reply.bulk = bulk;
        return reply;
    }

    static hv::RedisReply makeIntegerReply(int64_t value) {
        hv::RedisReply reply;
        reply.type = hv::REDIS_REPLY_INTEGER;
        reply.integer = value;
        return reply;
    }

    static hv::RedisReply makeErrorReply(const std::string& error) {
        hv::RedisReply reply;
        reply.type = hv::REDIS_REPLY_ERROR;
        reply.str = error;
        return reply;
    }

    static hv::RedisReply makeArrayReply(const std::string& kind, const std::string& name, int64_t count) {
        hv::RedisReply reply;
        reply.type = hv::REDIS_REPLY_ARRAY;
        reply.elements.push_back(makeStringReply(kind, true));
        reply.elements.push_back(makeStringReply(name, true));
        reply.elements.push_back(makeIntegerReply(count));
        return reply;
    }

    static bool commandEquals(const hv::RedisCommand& command, const char* verb) {
        return !command.empty() && command[0] == verb;
    }

    hv::RedisReply makePubSubReply(const hv::RedisCommand& command, const std::shared_ptr<ClientState>& state) {
        if (command.size() != 2) {
            return makeErrorReply("ERR wrong number of arguments");
        }
        const std::string& name = command[1];
        if (commandEquals(command, "SUBSCRIBE")) {
            state->channels.insert(name);
            return makeArrayReply("subscribe", name, (int64_t)(state->channels.size() + state->patterns.size()));
        }
        if (commandEquals(command, "PSUBSCRIBE")) {
            state->patterns.insert(name);
            return makeArrayReply("psubscribe", name, (int64_t)(state->channels.size() + state->patterns.size()));
        }
        if (commandEquals(command, "UNSUBSCRIBE")) {
            state->channels.erase(name);
            return makeArrayReply("unsubscribe", name, (int64_t)(state->channels.size() + state->patterns.size()));
        }
        state->patterns.erase(name);
        return makeArrayReply("punsubscribe", name, (int64_t)(state->channels.size() + state->patterns.size()));
    }

    hv::RedisReply makeReply(const hv::RedisReply& request, const std::shared_ptr<ClientState>& state) {
        if (!request.isArray()) {
            return makeErrorReply("ERR invalid command");
        }

        hv::RedisCommand command;
        command.reserve(request.elements.size());
        for (size_t i = 0; i < request.elements.size(); ++i) {
            const hv::RedisReply& arg = request.elements[i];
            if (!arg.isString()) {
                return makeErrorReply("ERR invalid command arg");
            }
            command.push_back(arg.str);
        }

        if (commandEquals(command, "SUBSCRIBE") || commandEquals(command, "PSUBSCRIBE") || commandEquals(command, "UNSUBSCRIBE") || commandEquals(command, "PUNSUBSCRIBE")) {
            return makePubSubReply(command, state);
        }

        if (handler) {
            return handler(command);
        }

        return makeStringReply("OK");
    }

    int publish(const std::string& channel, const std::string& message) {
        if (!server) {
            return 0;
        }

        int delivered = 0;
        server->foreachChannel([&](const hv::SocketChannelPtr& client) {
            std::shared_ptr<ClientState> state = client->getContextPtr<ClientState>();
            if (!state) {
                return;
            }
            if (state->channels.count(channel) != 0) {
                hv::RedisReply push;
                push.type = hv::REDIS_REPLY_ARRAY;
                push.elements.push_back(makeStringReply("message", true));
                push.elements.push_back(makeStringReply(channel, true));
                push.elements.push_back(makeStringReply(message, true));
                client->write(hv::RedisEncodeReply(push));
                ++delivered;
            }
            for (std::set<std::string>::const_iterator it = state->patterns.begin(); it != state->patterns.end(); ++it) {
                if (!hv_wildcard_match(channel.c_str(), it->c_str())) {
                    continue;
                }
                hv::RedisReply push;
                push.type = hv::REDIS_REPLY_ARRAY;
                push.elements.push_back(makeStringReply("pmessage", true));
                push.elements.push_back(makeStringReply(*it, true));
                push.elements.push_back(makeStringReply(channel, true));
                push.elements.push_back(makeStringReply(message, true));
                client->write(hv::RedisEncodeReply(push));
                ++delivered;
            }
        });
        return delivered;
    }
};

FakeRedisServer::FakeRedisServer()
    : impl_(std::make_shared<Impl>()) {}

FakeRedisServer::~FakeRedisServer() {
    stop();
}

void FakeRedisServer::setPort(int port) {
    impl_->port = port;
}

int FakeRedisServer::port() const {
    return impl_->port;
}

void FakeRedisServer::setCommandHandler(const std::function<hv::RedisReply(const hv::RedisCommand&)>& handler) {
    impl_->handler = handler;
}

void FakeRedisServer::closeClientAfterReply(bool enabled) {
    impl_->close_after_reply = enabled;
}

int FakeRedisServer::publish(const std::string& channel, const std::string& message) {
    return impl_->publish(channel, message);
}

void FakeRedisServer::start() {
    stop();
    impl_->server = std::make_shared<hv::TcpServer>();
    impl_->server->createsocket(impl_->port, "127.0.0.1");
    if (impl_->server->listenfd >= 0) {
        sockaddr_u addr;
        socklen_t len = sizeof(addr);
        memset(&addr, 0, sizeof(addr));
        if (getsockname(impl_->server->listenfd, &addr.sa, &len) == 0) {
            impl_->port = sockaddr_port(&addr);
        }
    }

    impl_->server->onConnection = [](const hv::SocketChannelPtr& channel) {
        if (channel->isConnected()) {
            channel->newContextPtr<Impl::ClientState>();
        }
    };

    impl_->server->onMessage = [this](const hv::SocketChannelPtr& channel, hv::Buffer* buf) {
        std::shared_ptr<Impl::ClientState> state = Impl::getOrCreateState(channel);
        state->parser.Feed((const char*)buf->data(), buf->size());
        if (state->parser.HasError()) {
            channel->close();
            return;
        }
        while (state->parser.HasReply()) {
            hv::RedisReply request = state->parser.NextReply();
            hv::RedisReply reply = impl_->makeReply(request, state);
            channel->write(hv::RedisEncodeReply(reply));
            if (impl_->close_after_reply) {
                channel->close();
                return;
            }
        }
    };

    impl_->server->start();
}

void FakeRedisServer::stop() {
    if (impl_->server) {
        impl_->server->stop(true);
        impl_->server.reset();
    }
}
