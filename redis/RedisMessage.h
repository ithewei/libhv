#ifndef HV_REDIS_MESSAGE_HPP_
#define HV_REDIS_MESSAGE_HPP_

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

namespace hv {

using RedisCommand = std::vector<std::string>;

enum RedisReplyType {
    REDIS_REPLY_NIL,
    REDIS_REPLY_STRING,
    REDIS_REPLY_ERROR,
    REDIS_REPLY_INTEGER,
    REDIS_REPLY_ARRAY,
};

struct RedisReply {
    RedisReplyType type = REDIS_REPLY_NIL;
    std::string str;
    int64_t integer = 0;
    std::vector<RedisReply> elements;
    bool bulk = false;
    bool null_array = false;

    bool isNil() const { return type == REDIS_REPLY_NIL; }
    bool isError() const { return type == REDIS_REPLY_ERROR; }
    bool isArray() const { return type == REDIS_REPLY_ARRAY; }
    bool isString() const { return type == REDIS_REPLY_STRING; }
    const std::string& error() const { return str; }
    const std::string& asString() const { return str; }
    int64_t asInt() const { return integer; }
    const std::vector<RedisReply>& asArray() const { return elements; }
};

struct RedisResult {
    int code;
    RedisReply reply;

    RedisResult(int c = 0)
        : code(c) {}

    bool ok() const { return code == 0 && !reply.isError(); }
};

template<typename T>
struct RedisValueResult {
    int code = 0;
    RedisReply reply;
    T value = T();
    bool has_value = false;

    bool ok() const { return code == 0 && !reply.isError() && has_value; }
    bool isNil() const { return code == 0 && reply.isNil(); }
};

std::string RedisEncodeCommand(const RedisCommand& command);
std::string RedisEncodeReply(const RedisReply& reply);

class RedisParser {
public:
    RedisParser();
    RedisParser(const RedisParser&) = delete;
    RedisParser& operator=(const RedisParser&) = delete;
    RedisParser(RedisParser&&) = default;
    RedisParser& operator=(RedisParser&&) = default;
    void Reset();
    size_t Feed(const char* data, size_t len);
    bool HasReply() const;
    RedisReply NextReply();
    bool HasError() const;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace hv

#endif // HV_REDIS_MESSAGE_HPP_
