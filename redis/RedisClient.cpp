#include "RedisClient.h"

#include <cctype>
#include <condition_variable>
#include <cstdarg>
#include <cstdint>
#include <mutex>
#include <utility>

#include "RedisPipeline.h"
#include "RedisTransaction.h"

namespace hv {

namespace {

template<typename T>
RedisValueResult<T> makeValueResult(const RedisResult& result);

template<>
RedisValueResult<std::string> makeValueResult<std::string>(const RedisResult& result) {
    RedisValueResult<std::string> out;
    out.code = result.code;
    out.reply = result.reply;
    if (result.code == 0 && result.reply.type == REDIS_REPLY_STRING && !result.reply.isError()) {
        out.value = result.reply.asString();
        out.has_value = true;
    }
    return out;
}

template<>
RedisValueResult<int64_t> makeValueResult<int64_t>(const RedisResult& result) {
    RedisValueResult<int64_t> out;
    out.code = result.code;
    out.reply = result.reply;
    if (result.code == 0 && result.reply.type == REDIS_REPLY_INTEGER && !result.reply.isError()) {
        out.value = result.reply.asInt();
        out.has_value = true;
    }
    return out;
}

template<typename T>
RedisValueResult<T> commandValue(RedisClient* client, const RedisCommand& command) {
    return makeValueResult<T>(client->command(command));
}

template<typename T>
int commandAsyncValue(RedisClient* client, const RedisCommand& command, RedisValueCallback<T> cb) {
    return client->commandAsync(command, [cb](const RedisResult& result) {
        if (cb) {
            cb(makeValueResult<T>(result));
        }
    });
}

} // namespace

RedisClient::RedisClient() {}

void RedisClient::setHost(const std::string& host) {
    async_.setHost(host);
}

void RedisClient::setPort(int port) {
    async_.setPort(port);
}

void RedisClient::setAuth(const std::string& password) {
    async_.setAuth(password);
}

void RedisClient::setDb(int db) {
    async_.setDb(db);
}

void RedisClient::setConnectTimeout(int ms) {
    async_.setConnectTimeout(ms);
}

void RedisClient::setTimeout(int ms) {
    async_.setTimeout(ms);
}

void RedisClient::setReconnect(reconn_setting_t* setting) {
    async_.setReconnect(setting);
}

RedisResult RedisClient::command(const RedisCommand& command) {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    RedisResult result;

    if (async_.isStarted() && async_.isInLoopThread()) {
        return RedisResult(ERR_INVALID_HANDLE);
    }
    if (!async_.isStarted()) {
        async_.start();
    }
    int ret = async_.command(command, [&](const RedisResult& value) {
        std::lock_guard<std::mutex> lock(mutex);
        result = value;
        done = true;
        cv.notify_one();
    });
    if (ret != 0) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!done) {
            result.code = ret;
        }
        return result;
    }

    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [&done]() { return done; });
    return result;
}

int RedisClient::commandAsync(const RedisCommand& command, RedisCallback cb) {
    if (!async_.isStarted()) {
        async_.start();
    }
    return async_.command(command, std::move(cb));
}

RedisResult RedisClient::commandf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap_copy;
    va_copy(ap_copy, ap);
    int len = vsnprintf(NULL, 0, fmt, ap_copy);
    va_end(ap_copy);
    if (len < 0) {
        va_end(ap);
        return RedisResult(ERR_INVALID_PARAM);
    }
    std::vector<char> buf((size_t)len + 1);
    if (vsnprintf(buf.data(), buf.size(), fmt, ap) < 0) {
        va_end(ap);
        return RedisResult(ERR_INVALID_PARAM);
    }
    va_end(ap);

    RedisCommand command = tokenize(buf.data());
    if (command.empty()) {
        return RedisResult(ERR_INVALID_PARAM);
    }
    return this->command(command);
}

RedisPipeline RedisClient::pipeline() {
    return RedisPipeline(this);
}

RedisTransaction RedisClient::transaction() {
    return RedisTransaction(this);
}

RedisValueResult<std::string> RedisClient::get(const std::string& key) {
    return commandValue<std::string>(this, RedisCommand{"GET", key});
}

int RedisClient::getAsync(const std::string& key, RedisValueCallback<std::string> cb) {
    return commandAsyncValue<std::string>(this, RedisCommand{"GET", key}, std::move(cb));
}

RedisResult RedisClient::set(const std::string& key, const std::string& value) {
    return command(RedisCommand{"SET", key, value});
}

int RedisClient::setAsync(const std::string& key, const std::string& value, RedisCallback cb) {
    return commandAsync(RedisCommand{"SET", key, value}, std::move(cb));
}

RedisValueResult<int64_t> RedisClient::del(const std::string& key) {
    return commandValue<int64_t>(this, RedisCommand{"DEL", key});
}

int RedisClient::delAsync(const std::string& key, RedisValueCallback<int64_t> cb) {
    return commandAsyncValue<int64_t>(this, RedisCommand{"DEL", key}, std::move(cb));
}

RedisValueResult<int64_t> RedisClient::exists(const std::string& key) {
    return commandValue<int64_t>(this, RedisCommand{"EXISTS", key});
}

int RedisClient::existsAsync(const std::string& key, RedisValueCallback<int64_t> cb) {
    return commandAsyncValue<int64_t>(this, RedisCommand{"EXISTS", key}, std::move(cb));
}

RedisValueResult<int64_t> RedisClient::expire(const std::string& key, int seconds) {
    return commandValue<int64_t>(this, RedisCommand{"EXPIRE", key, std::to_string(seconds)});
}

int RedisClient::expireAsync(const std::string& key, int seconds, RedisValueCallback<int64_t> cb) {
    return commandAsyncValue<int64_t>(this, RedisCommand{"EXPIRE", key, std::to_string(seconds)}, std::move(cb));
}

RedisValueResult<std::string> RedisClient::hget(const std::string& key, const std::string& field) {
    return commandValue<std::string>(this, RedisCommand{"HGET", key, field});
}

int RedisClient::hgetAsync(const std::string& key, const std::string& field, RedisValueCallback<std::string> cb) {
    return commandAsyncValue<std::string>(this, RedisCommand{"HGET", key, field}, std::move(cb));
}

RedisValueResult<int64_t> RedisClient::hset(const std::string& key, const std::string& field, const std::string& value) {
    return commandValue<int64_t>(this, RedisCommand{"HSET", key, field, value});
}

int RedisClient::hsetAsync(const std::string& key, const std::string& field, const std::string& value, RedisValueCallback<int64_t> cb) {
    return commandAsyncValue<int64_t>(this, RedisCommand{"HSET", key, field, value}, std::move(cb));
}

RedisValueResult<int64_t> RedisClient::publish(const std::string& channel, const std::string& message) {
    return commandValue<int64_t>(this, RedisCommand{"PUBLISH", channel, message});
}

int RedisClient::publishAsync(const std::string& channel, const std::string& message, RedisValueCallback<int64_t> cb) {
    return commandAsyncValue<int64_t>(this, RedisCommand{"PUBLISH", channel, message}, std::move(cb));
}

RedisResult RedisClient::commandBatch(const std::vector<RedisCommand>& commands, std::vector<RedisReply>* replies) {
    if (replies) {
        replies->clear();
    }
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    RedisResult result;
    std::vector<RedisReply> batch_replies;

    if (async_.isStarted() && async_.isInLoopThread()) {
        return RedisResult(ERR_INVALID_HANDLE);
    }
    if (!async_.isStarted()) {
        async_.start();
    }
    int ret = async_.commandBatch(commands, [&](int code, const std::vector<RedisReply>& value) {
        std::lock_guard<std::mutex> lock(mutex);
        result.code = code;
        if (code == 0) {
            batch_replies = value;
        }
        done = true;
        cv.notify_one();
    });
    if (ret != 0) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!done) {
            result.code = ret;
        }
        return result;
    }

    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [&done]() { return done; });
    lock.unlock();

    if (result.code == 0) {
        if (replies) {
            *replies = batch_replies;
        }
        if (!batch_replies.empty()) {
            result.reply = batch_replies.back();
        }
    }
    return result;
}

int RedisClient::commandBatchAsync(const std::vector<RedisCommand>& commands, RedisRepliesCallback cb) {
    if (!async_.isStarted()) {
        async_.start();
    }
    return async_.commandBatch(commands, std::move(cb));
}

RedisCommand RedisClient::tokenize(const std::string& line) {
    // Quote-aware splitting (same spirit as redis-cli's sdssplitargs) so that
    // arguments containing whitespace can be passed via commandf, e.g.
    //   commandf("SET %s %s", "key", "\"hello world\"")
    // Without this, a value with spaces would be split into multiple tokens and
    // a wrong command would be sent silently. On an unterminated quote we return
    // an empty command; the caller maps that to ERR_INVALID_PARAM.
    RedisCommand command;
    size_t i = 0;
    size_t n = line.size();
    while (i < n) {
        // skip leading whitespace between tokens
        while (i < n && std::isspace((unsigned char)line[i])) {
            ++i;
        }
        if (i >= n) {
            break;
        }
        std::string token;
        bool in_token = true;
        while (in_token && i < n) {
            char ch = line[i];
            if (ch == '"') {
                // double-quoted: honor backslash escapes, must be closed
                ++i;
                while (i < n && line[i] != '"') {
                    if (line[i] == '\\' && i + 1 < n) {
                        ++i;
                    }
                    token.push_back(line[i]);
                    ++i;
                }
                if (i >= n) {
                    return RedisCommand(); // unterminated quote
                }
                ++i; // consume closing quote
            }
            else if (ch == '\'') {
                // single-quoted: literal, '' inside means a single quote char
                ++i;
                while (i < n && line[i] != '\'') {
                    token.push_back(line[i]);
                    ++i;
                }
                if (i >= n) {
                    return RedisCommand(); // unterminated quote
                }
                ++i; // consume closing quote
            }
            else if (std::isspace((unsigned char)ch)) {
                in_token = false;
            }
            else {
                token.push_back(ch);
                ++i;
            }
        }
        command.push_back(token);
    }
    return command;
}

} // namespace hv
