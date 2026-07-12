#include "redis/RedisClient.h"
#include "redis_test_server.h"

#include <assert.h>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <vector>

using namespace hv;

struct RedisBatchFixture {
    std::map<std::string, int64_t> kv;
    std::vector<RedisCommand> commands;
    std::vector<RedisCommand> transaction_queue;
    bool in_multi = false;
    bool abort_exec = false;
    bool queue_error = false;
    bool exec_error_reply = false;
    FakeRedisServer server;

    RedisBatchFixture() {
        server.setCommandHandler([this](const RedisCommand& cmd) {
            commands.push_back(cmd);
            RedisReply reply;
            if (cmd.empty()) {
                reply.type = REDIS_REPLY_ERROR;
                reply.str = "ERR empty command";
                return reply;
            }

            if (cmd[0] == "AUTH" || cmd[0] == "SELECT") {
                reply.type = REDIS_REPLY_STRING;
                reply.str = "OK";
                return reply;
            }

            if (cmd[0] == "MULTI") {
                in_multi = true;
                transaction_queue.clear();
                reply.type = REDIS_REPLY_STRING;
                reply.str = "OK";
                return reply;
            }

            if (cmd[0] == "DISCARD") {
                in_multi = false;
                transaction_queue.clear();
                reply.type = REDIS_REPLY_STRING;
                reply.str = "OK";
                return reply;
            }

            if (cmd[0] == "EXEC") {
                if (abort_exec) {
                    in_multi = false;
                    transaction_queue.clear();
                    reply.type = REDIS_REPLY_NIL;
                    return reply;
                }
                reply.type = REDIS_REPLY_ARRAY;
                if (exec_error_reply) {
                    RedisReply error;
                    error.type = REDIS_REPLY_ERROR;
                    error.str = "ERR exec failed";
                    reply.elements.push_back(error);
                }
                else {
                    for (size_t i = 0; i < transaction_queue.size(); ++i) {
                        reply.elements.push_back(execute(transaction_queue[i]));
                    }
                }
                in_multi = false;
                transaction_queue.clear();
                return reply;
            }

            if (in_multi) {
                transaction_queue.push_back(cmd);
                if (queue_error && cmd[0] == "NOPE") {
                    reply.type = REDIS_REPLY_ERROR;
                    reply.str = "ERR queue rejected";
                    return reply;
                }
                reply.type = REDIS_REPLY_STRING;
                reply.str = "QUEUED";
                return reply;
            }

            return execute(cmd);
        });
        server.start();
    }

    ~RedisBatchFixture() {
        server.stop();
    }

    RedisReply execute(const RedisCommand& cmd) {
        RedisReply reply;
        if (cmd[0] == "SET") {
            kv[cmd[1]] = atoll(cmd[2].c_str());
            reply.type = REDIS_REPLY_STRING;
            reply.str = "OK";
        }
        else if (cmd[0] == "GET") {
            std::map<std::string, int64_t>::const_iterator it = kv.find(cmd[1]);
            if (it == kv.end()) {
                reply.type = REDIS_REPLY_NIL;
            }
            else {
                reply.type = REDIS_REPLY_STRING;
                reply.str = std::to_string(it->second);
                reply.bulk = true;
            }
        }
        else if (cmd[0] == "INCR") {
            reply.type = REDIS_REPLY_INTEGER;
            reply.integer = ++kv[cmd[1]];
        }
        else {
            reply.type = REDIS_REPLY_ERROR;
            reply.str = "ERR unsupported";
        }
        return reply;
    }
};

static void setupClient(RedisClient* client, int port) {
    client->setHost("127.0.0.1");
    client->setPort(port);
    client->setAuth("secret");
    client->setDb(3);
    client->setTimeout(3000);
    client->setConnectTimeout(3000);
}

static void test_pipeline_sync_exec() {
    RedisBatchFixture fixture;
    RedisClient client;
    setupClient(&client, fixture.server.port());

    std::vector<RedisReply> replies;
    RedisPipeline pipeline = client.pipeline();
    pipeline.appendCommand(RedisCommand{"SET", "counter", "1"});
    pipeline.appendCommand(RedisCommand{"INCR", "counter"});

    RedisResult result = pipeline.exec(&replies);
    assert(result.code == 0);
    assert(result.ok());
    assert(replies.size() == 2);
    assert(replies[0].asString() == "OK");
    assert(replies[1].type == REDIS_REPLY_INTEGER);
    assert(replies[1].asInt() == 2);
    assert(fixture.kv["counter"] == 2);

    std::vector<RedisReply> second_replies;
    RedisResult second = pipeline.exec(&second_replies);
    assert(second.code == ERR_INVALID_PARAM);
    assert(second_replies.empty());
}

static void test_pipeline_async_exec() {
    RedisBatchFixture fixture;
    RedisClient client;
    setupClient(&client, fixture.server.port());

    RedisPipeline pipeline = client.pipeline();
    pipeline.appendCommand(RedisCommand{"SET", "async-counter", "4"});
    pipeline.appendCommand(RedisCommand{"INCR", "async-counter"});

    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    int code = ERR_UNKNOWN;
    std::vector<RedisReply> replies;

    int ret = pipeline.execAsync([&](int batch_code, const std::vector<RedisReply>& batch_replies) {
        std::lock_guard<std::mutex> lock(mutex);
        code = batch_code;
        replies = batch_replies;
        done = true;
        cv.notify_one();
    });
    assert(ret == 0);

    std::unique_lock<std::mutex> lock(mutex);
    bool completed = cv.wait_for(lock, std::chrono::seconds(3), [&done]() { return done; });
    assert(completed);
    assert(code == 0);
    assert(replies.size() == 2);
    assert(replies[0].asString() == "OK");
    assert(replies[1].asInt() == 5);
    assert(fixture.kv["async-counter"] == 5);

    int second = pipeline.execAsync(NULL);
    assert(second == ERR_INVALID_PARAM);
}

static void test_pipeline_error_reply_marks_failure() {
    RedisBatchFixture fixture;
    RedisClient client;
    setupClient(&client, fixture.server.port());

    std::vector<RedisReply> replies;
    RedisPipeline pipeline = client.pipeline();
    pipeline.appendCommand(RedisCommand{"SET", "counter", "1"});
    pipeline.appendCommand(RedisCommand{"NOPE"});
    pipeline.appendCommand(RedisCommand{"INCR", "counter"});

    RedisResult result = pipeline.exec(&replies);
    assert(result.code == 0);
    assert(!result.ok());
    assert(result.reply.isError());
    assert(result.reply.error() == "ERR unsupported");
    assert(replies.size() == 3);
    assert(replies[0].asString() == "OK");
    assert(replies[1].isError());
    assert(replies[2].type == REDIS_REPLY_INTEGER);
    assert(replies[2].asInt() == 2);
}

static void test_transaction_exec() {
    RedisBatchFixture fixture;
    RedisClient client;
    setupClient(&client, fixture.server.port());

    RedisTransaction transaction = client.transaction();
    transaction.appendCommand(RedisCommand{"SET", "tx-counter", "10"});
    transaction.appendCommand(RedisCommand{"INCR", "tx-counter"});

    std::vector<RedisReply> replies;
    RedisResult result = transaction.exec(&replies);
    assert(result.code == 0);
    assert(result.ok());
    assert(result.reply.type == REDIS_REPLY_ARRAY);
    assert(replies.size() == 2);
    assert(replies[0].asString() == "OK");
    assert(replies[1].type == REDIS_REPLY_INTEGER);
    assert(replies[1].asInt() == 11);
    assert(fixture.kv["tx-counter"] == 11);
    assert(fixture.commands.size() >= 6);
    assert(fixture.commands[fixture.commands.size() - 4][0] == "MULTI");
    assert(fixture.commands[fixture.commands.size() - 3][0] == "SET");
    assert(fixture.commands[fixture.commands.size() - 2][0] == "INCR");
    assert(fixture.commands[fixture.commands.size() - 1][0] == "EXEC");

    std::vector<RedisReply> second_replies;
    RedisResult second = transaction.exec(&second_replies);
    assert(second.code == ERR_INVALID_PARAM);
    assert(second_replies.empty());
}

static void test_transaction_exec_nil_reply_fails() {
    RedisBatchFixture fixture;
    fixture.abort_exec = true;
    RedisClient client;
    setupClient(&client, fixture.server.port());

    RedisTransaction transaction = client.transaction();
    transaction.appendCommand(RedisCommand{"SET", "aborted", "1"});

    std::vector<RedisReply> replies;
    RedisResult result = transaction.exec(&replies);
    assert(result.code == ERR_RESPONSE);
    assert(!result.ok());
    assert(result.reply.isNil());
    assert(replies.empty());
}

static void test_transaction_exec_error_reply_fails() {
    RedisBatchFixture fixture;
    fixture.exec_error_reply = true;
    RedisClient client;
    setupClient(&client, fixture.server.port());

    RedisTransaction transaction = client.transaction();
    transaction.appendCommand(RedisCommand{"SET", "broken", "1"});

    std::vector<RedisReply> replies;
    RedisResult result = transaction.exec(&replies);
    assert(result.code == 0);
    assert(!result.ok());
    assert(result.reply.isError());
    assert(result.reply.error() == "ERR exec failed");
    assert(replies.size() == 1);
    assert(replies[0].isError());
}

static void test_transaction_queue_error_fails() {
    RedisBatchFixture fixture;
    fixture.queue_error = true;
    RedisClient client;
    setupClient(&client, fixture.server.port());

    RedisTransaction transaction = client.transaction();
    transaction.appendCommand(RedisCommand{"NOPE"});

    std::vector<RedisReply> replies;
    RedisResult result = transaction.exec(&replies);
    assert(result.code == 0);
    assert(!result.ok());
    assert(result.reply.isError());
    assert(result.reply.error() == "ERR queue rejected");
    assert(replies.empty());
}

static void test_transaction_discard() {
    RedisBatchFixture fixture;
    RedisClient client;
    setupClient(&client, fixture.server.port());

    RedisTransaction transaction = client.transaction();
    transaction.appendCommand(RedisCommand{"SET", "discarded", "8"});
    RedisResult discard_result = transaction.discard();
    assert(discard_result.code == 0);
    assert(discard_result.ok());
    assert(discard_result.reply.asString() == "OK");

    std::vector<RedisReply> replies;
    RedisResult exec_result = transaction.exec(&replies);
    assert(exec_result.code == ERR_INVALID_PARAM);
    assert(replies.empty());
    assert(fixture.kv.count("discarded") == 0);
    for (size_t i = 0; i < fixture.commands.size(); ++i) {
        assert(fixture.commands[i][0] != "MULTI");
        assert(fixture.commands[i][0] != "EXEC");
    }

    RedisResult second_discard = transaction.discard();
    assert(second_discard.code == ERR_INVALID_PARAM);
}

static void test_redis_client_header_is_self_sufficient() {
    RedisClient client;
    RedisPipeline pipeline = client.pipeline();
    RedisTransaction transaction = client.transaction();
    (void)pipeline;
    (void)transaction;
}

int main() {
    test_pipeline_sync_exec();
    test_pipeline_async_exec();
    test_pipeline_error_reply_marks_failure();
    test_transaction_exec();
    test_transaction_exec_nil_reply_fails();
    test_transaction_exec_error_reply_fails();
    test_transaction_queue_error_fails();
    test_transaction_discard();
    test_redis_client_header_is_self_sufficient();
    return 0;
}
