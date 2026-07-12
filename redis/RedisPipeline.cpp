#include "RedisPipeline.h"

#include <utility>

#include "RedisClient.h"

namespace hv {

namespace {

const RedisReply* firstErrorReply(const std::vector<RedisReply>& replies) {
    for (size_t i = 0; i < replies.size(); ++i) {
        if (replies[i].isError()) {
            return &replies[i];
        }
    }
    return NULL;
}

} // namespace

RedisPipeline::RedisPipeline(RedisClient* client)
    : client_(client) {}

void RedisPipeline::appendCommand(const RedisCommand& command) {
    commands_.push_back(command);
}

RedisResult RedisPipeline::exec(std::vector<RedisReply>* replies) {
    if (client_ == NULL || commands_.empty()) {
        if (replies) {
            replies->clear();
        }
        return RedisResult(ERR_INVALID_PARAM);
    }

    std::vector<RedisReply> batch_replies;
    std::vector<RedisReply>* output = replies ? replies : &batch_replies;
    RedisResult result = client_->commandBatch(commands_, output);
    if (result.code != 0) {
        return result;
    }

    commands_.clear();

    const RedisReply* error_reply = firstErrorReply(*output);
    if (error_reply != NULL) {
        result.reply = *error_reply;
    }
    else if (!output->empty()) {
        result.reply = output->back();
    }
    return result;
}

int RedisPipeline::execAsync(const RedisRepliesCallback& cb) {
    if (client_ == NULL || commands_.empty()) {
        if (cb) {
            cb(ERR_INVALID_PARAM, std::vector<RedisReply>());
        }
        return ERR_INVALID_PARAM;
    }
    int ret = client_->commandBatchAsync(commands_, cb);
    if (ret == 0) {
        commands_.clear();
    }
    return ret;
}

} // namespace hv
