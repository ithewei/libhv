#include "RedisTransaction.h"

#include "RedisClient.h"

namespace hv {

RedisTransaction::RedisTransaction(RedisClient* client)
    : client_(client) {}

void RedisTransaction::appendCommand(const RedisCommand& command) {
    commands_.push_back(command);
}

RedisResult RedisTransaction::exec(std::vector<RedisReply>* replies) {
    if (replies) {
        replies->clear();
    }
    if (client_ == NULL || commands_.empty()) {
        return RedisResult(ERR_INVALID_PARAM);
    }

    std::vector<RedisCommand> batch;
    batch.reserve(commands_.size() + 2);
    batch.push_back(RedisCommand{"MULTI"});
    batch.insert(batch.end(), commands_.begin(), commands_.end());
    batch.push_back(RedisCommand{"EXEC"});

    std::vector<RedisReply> batch_replies;
    RedisResult result = client_->commandBatch(batch, &batch_replies);
    if (result.code != 0) {
        return result;
    }
    commands_.clear();
    if (batch_replies.size() != batch.size()) {
        return RedisResult(ERR_RESPONSE);
    }

    const RedisReply& multi_reply = batch_replies.front();
    if (multi_reply.isError()) {
        result.reply = multi_reply;
        return result;
    }

    for (size_t i = 1; i + 1 < batch_replies.size(); ++i) {
        if (batch_replies[i].isError()) {
            result.reply = batch_replies[i];
            return result;
        }
    }

    const RedisReply& exec_reply = batch_replies.back();
    result.reply = exec_reply;
    if (exec_reply.isNil() || !exec_reply.isArray()) {
        result.code = ERR_RESPONSE;
        return result;
    }
    if (replies != NULL) {
        *replies = exec_reply.elements;
    }
    for (size_t i = 0; i < exec_reply.elements.size(); ++i) {
        if (exec_reply.elements[i].isError()) {
            result.reply = exec_reply.elements[i];
            return result;
        }
    }
    return result;
}

RedisResult RedisTransaction::discard() {
    if (client_ == NULL || commands_.empty()) {
        return RedisResult(ERR_INVALID_PARAM);
    }
    commands_.clear();
    RedisResult result;
    result.reply.type = REDIS_REPLY_STRING;
    result.reply.str = "OK";
    return result;
}

} // namespace hv
