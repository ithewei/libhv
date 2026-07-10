#include "RedisMessage.h"

#include <cerrno>
#include <cstdlib>
#include <deque>
#include <utility>

namespace hv {

namespace {

void RedisAppendBulk(std::string& out, const std::string& value) {
    out += "$";
    out += std::to_string(value.size());
    out += "\r\n";
    out += value;
    out += "\r\n";
}

void RedisAppendReply(std::string& out, const RedisReply& reply) {
    switch (reply.type) {
    case REDIS_REPLY_STRING:
        if (reply.bulk) {
            RedisAppendBulk(out, reply.str);
        }
        else {
            out += "+";
            out += reply.str;
            out += "\r\n";
        }
        return;
    case REDIS_REPLY_ERROR:
        out += "-";
        out += reply.str;
        out += "\r\n";
        return;
    case REDIS_REPLY_INTEGER:
        out += ":";
        out += std::to_string(reply.integer);
        out += "\r\n";
        return;
    case REDIS_REPLY_ARRAY:
        out += "*";
        out += std::to_string(reply.elements.size());
        out += "\r\n";
        for (size_t i = 0; i < reply.elements.size(); ++i) {
            RedisAppendReply(out, reply.elements[i]);
        }
        return;
    case REDIS_REPLY_NIL:
        out += reply.null_array ? "*-1\r\n" : "$-1\r\n";
        return;
    default:
        out += "$-1\r\n";
        return;
    }
}

} // namespace

struct RedisParser::Impl {
    enum ParseStatus {
        kParseOk,
        kParseIncomplete,
        kParseError,
    };

    std::string buffer;
    std::deque<RedisReply> replies;
    bool error;

    Impl()
        : error(false) {}

    static bool ParseInt64(const std::string& str, int64_t* value) {
        if (value == NULL || str.empty()) return false;
        char* end = NULL;
        errno = 0;
        long long parsed = strtoll(str.c_str(), &end, 10);
        if (errno != 0 || end == str.c_str() || *end != '\0') {
            return false;
        }
        *value = (int64_t)parsed;
        return true;
    }

    bool ParseLine(size_t start, std::string& line, size_t& next) const {
        size_t pos = buffer.find("\r\n", start);
        if (pos == std::string::npos) {
            return false;
        }
        line.assign(buffer, start, pos - start);
        next = pos + 2;
        return true;
    }

    ParseStatus ParseOne(size_t& off, RedisReply& reply) {
        size_t start = off;
        if (off >= buffer.size()) {
            return kParseIncomplete;
        }

        char prefix = buffer[off++];
        std::string line;
        size_t next = off;
        int64_t number = 0;

        switch (prefix) {
        case '+':
            if (!ParseLine(off, line, next)) {
                off = start;
                return kParseIncomplete;
            }
            reply = RedisReply();
            reply.type = REDIS_REPLY_STRING;
            reply.str = line;
            off = next;
            return kParseOk;
        case '-':
            if (!ParseLine(off, line, next)) {
                off = start;
                return kParseIncomplete;
            }
            reply = RedisReply();
            reply.type = REDIS_REPLY_ERROR;
            reply.str = line;
            off = next;
            return kParseOk;
        case ':':
            if (!ParseLine(off, line, next)) {
                off = start;
                return kParseIncomplete;
            }
            if (!ParseInt64(line, &number)) {
                off = next;
                return kParseError;
            }
            reply = RedisReply();
            reply.type = REDIS_REPLY_INTEGER;
            reply.integer = number;
            off = next;
            return kParseOk;
        case '$': {
            if (!ParseLine(off, line, next)) {
                off = start;
                return kParseIncomplete;
            }
            if (!ParseInt64(line, &number)) {
                off = next;
                return kParseError;
            }
            if (number == -1) {
                reply = RedisReply();
                off = next;
                return kParseOk;
            }
            if (number < 0) {
                off = next;
                return kParseError;
            }
            size_t bulk_len = (size_t)number;
            if (buffer.size() < next + bulk_len + 2) {
                off = start;
                return kParseIncomplete;
            }
            if (buffer[next + bulk_len] != '\r' || buffer[next + bulk_len + 1] != '\n') {
                off = next + bulk_len + 2;
                return kParseError;
            }
            reply = RedisReply();
            reply.type = REDIS_REPLY_STRING;
            reply.bulk = true;
            reply.str.assign(buffer.data() + next, bulk_len);
            off = next + bulk_len + 2;
            return kParseOk;
        }
        case '*': {
            if (!ParseLine(off, line, next)) {
                off = start;
                return kParseIncomplete;
            }
            if (!ParseInt64(line, &number)) {
                off = next;
                return kParseError;
            }
            if (number == -1) {
                reply = RedisReply();
                reply.null_array = true;
                off = next;
                return kParseOk;
            }
            if (number < 0) {
                off = next;
                return kParseError;
            }
            size_t cursor = next;
            std::vector<RedisReply> elements;
            elements.reserve((size_t)number);
            for (int64_t i = 0; i < number; ++i) {
                RedisReply element;
                ParseStatus status = ParseOne(cursor, element);
                if (status == kParseIncomplete) {
                    off = start;
                    return status;
                }
                if (status == kParseError) {
                    off = cursor;
                    return status;
                }
                elements.push_back(std::move(element));
            }
            reply = RedisReply();
            reply.type = REDIS_REPLY_ARRAY;
            reply.elements.swap(elements);
            off = cursor;
            return kParseOk;
        }
        default:
            off = start;
            return kParseError;
        }
    }

    void ParseAll() {
        size_t off = 0;
        error = false;
        while (off < buffer.size()) {
            RedisReply reply;
            size_t next = off;
            ParseStatus status = ParseOne(next, reply);
            if (status == kParseOk) {
                replies.push_back(std::move(reply));
                off = next;
                continue;
            }
            if (status == kParseIncomplete) {
                break;
            }
            error = true;
            off = next > off ? next : off + 1;
        }
        if (off != 0) {
            buffer.erase(0, off);
        }
    }
};

std::string RedisEncodeCommand(const RedisCommand& command) {
    std::string out = "*" + std::to_string(command.size()) + "\r\n";
    for (size_t i = 0; i < command.size(); ++i) {
        RedisAppendBulk(out, command[i]);
    }
    return out;
}

std::string RedisEncodeReply(const RedisReply& reply) {
    std::string out;
    RedisAppendReply(out, reply);
    return out;
}

RedisParser::RedisParser()
    : impl_(std::make_shared<Impl>()) {}

void RedisParser::Reset() {
    impl_->buffer.clear();
    impl_->replies.clear();
    impl_->error = false;
}

size_t RedisParser::Feed(const char* data, size_t len) {
    if (data != NULL && len != 0) {
        impl_->buffer.append(data, len);
    }
    impl_->ParseAll();
    return len;
}

bool RedisParser::HasReply() const {
    return !impl_->replies.empty();
}

RedisReply RedisParser::NextReply() {
    if (impl_->replies.empty()) {
        return RedisReply();
    }
    RedisReply reply = std::move(impl_->replies.front());
    impl_->replies.pop_front();
    return reply;
}

bool RedisParser::HasError() const {
    return impl_->error;
}

} // namespace hv
