#include "redis/RedisClient.h"
#include "redis_test_server.h"

#include <assert.h>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <vector>

using namespace hv;

struct RedisFixture {
    std::map<std::string, std::string> kv;
    std::map<std::string, std::map<std::string, std::string> > hashes;
    std::vector<std::string> published_messages;
    std::vector<RedisCommand> commands;
    FakeRedisServer server;

    RedisFixture() {
        server.setCommandHandler([this](const RedisCommand& cmd) {
            commands.push_back(cmd);
            RedisReply reply;
            if (cmd.empty()) {
                reply.type = REDIS_REPLY_ERROR;
                reply.str = "ERR empty command";
                return reply;
            }
            if (cmd[0] == "AUTH") {
                if (cmd.size() != 2) {
                    reply.type = REDIS_REPLY_ERROR;
                    reply.str = "ERR wrong number of arguments for 'AUTH'";
                    return reply;
                }
                reply.type = REDIS_REPLY_STRING;
                reply.str = "OK";
            }
            else if (cmd[0] == "SELECT") {
                if (cmd.size() != 2) {
                    reply.type = REDIS_REPLY_ERROR;
                    reply.str = "ERR wrong number of arguments for 'SELECT'";
                    return reply;
                }
                reply.type = REDIS_REPLY_STRING;
                reply.str = "OK";
            }
            else if (cmd[0] == "SET") {
                if (cmd.size() != 3) {
                    reply.type = REDIS_REPLY_ERROR;
                    reply.str = "ERR wrong number of arguments for 'SET'";
                    return reply;
                }
                kv[cmd[1]] = cmd[2];
                reply.type = REDIS_REPLY_STRING;
                reply.str = "OK";
            }
            else if (cmd[0] == "GET") {
                if (cmd.size() != 2) {
                    reply.type = REDIS_REPLY_ERROR;
                    reply.str = "ERR wrong number of arguments for 'GET'";
                    return reply;
                }
                std::map<std::string, std::string>::const_iterator it = kv.find(cmd[1]);
                if (it == kv.end()) {
                    reply.type = REDIS_REPLY_NIL;
                }
                else {
                    reply.type = REDIS_REPLY_STRING;
                    reply.str = it->second;
                    reply.bulk = true;
                }
            }
            else if (cmd[0] == "DEL") {
                if (cmd.size() != 2) {
                    reply.type = REDIS_REPLY_ERROR;
                    reply.str = "ERR wrong number of arguments for 'DEL'";
                    return reply;
                }
                reply.type = REDIS_REPLY_INTEGER;
                reply.integer = kv.erase(cmd[1]);
            }
            else if (cmd[0] == "EXISTS") {
                if (cmd.size() != 2) {
                    reply.type = REDIS_REPLY_ERROR;
                    reply.str = "ERR wrong number of arguments for 'EXISTS'";
                    return reply;
                }
                reply.type = REDIS_REPLY_INTEGER;
                reply.integer = kv.count(cmd[1]) ? 1 : 0;
            }
            else if (cmd[0] == "EXPIRE") {
                if (cmd.size() != 3) {
                    reply.type = REDIS_REPLY_ERROR;
                    reply.str = "ERR wrong number of arguments for 'EXPIRE'";
                    return reply;
                }
                reply.type = REDIS_REPLY_INTEGER;
                reply.integer = kv.count(cmd[1]) ? 1 : 0;
            }
            else if (cmd[0] == "HSET") {
                if (cmd.size() != 4) {
                    reply.type = REDIS_REPLY_ERROR;
                    reply.str = "ERR wrong number of arguments for 'HSET'";
                    return reply;
                }
                bool inserted = hashes[cmd[1]].count(cmd[2]) == 0;
                hashes[cmd[1]][cmd[2]] = cmd[3];
                reply.type = REDIS_REPLY_INTEGER;
                reply.integer = inserted ? 1 : 0;
            }
            else if (cmd[0] == "HGET") {
                if (cmd.size() != 3) {
                    reply.type = REDIS_REPLY_ERROR;
                    reply.str = "ERR wrong number of arguments for 'HGET'";
                    return reply;
                }
                std::map<std::string, std::map<std::string, std::string> >::const_iterator hash_it = hashes.find(cmd[1]);
                if (hash_it == hashes.end() || hash_it->second.count(cmd[2]) == 0) {
                    reply.type = REDIS_REPLY_NIL;
                }
                else {
                    reply.type = REDIS_REPLY_STRING;
                    reply.str = hash_it->second.find(cmd[2])->second;
                    reply.bulk = true;
                }
            }
            else if (cmd[0] == "PUBLISH") {
                if (cmd.size() != 3) {
                    reply.type = REDIS_REPLY_ERROR;
                    reply.str = "ERR wrong number of arguments for 'PUBLISH'";
                    return reply;
                }
                published_messages.push_back(cmd[1] + ":" + cmd[2]);
                reply.type = REDIS_REPLY_INTEGER;
                reply.integer = 1;
            }
            else {
                reply.type = REDIS_REPLY_ERROR;
                reply.str = "ERR unsupported";
            }
            return reply;
        });
        server.start();
    }

    ~RedisFixture() {
        server.stop();
    }
};

static void setupClient(RedisClient* client, int port) {
    client->setHost("127.0.0.1");
    client->setPort(port);
    client->setAuth("secret");
    client->setDb(2);
    client->setTimeout(3000);
    client->setConnectTimeout(3000);
}

static bool hasCommand(const std::vector<RedisCommand>& commands, const char* name) {
    for (size_t i = 0; i < commands.size(); ++i) {
        if (!commands[i].empty() && commands[i][0] == name) {
            return true;
        }
    }
    return false;
}

static void test_sync_command_and_helpers() {
    RedisFixture fixture;
    RedisClient client;
    setupClient(&client, fixture.server.port());

    RedisResult set_result = client.set("name", "libhv");
    assert(set_result.code == 0);
    assert(set_result.ok());
    assert(set_result.reply.asString() == "OK");
    assert(hasCommand(fixture.commands, "AUTH"));
    assert(hasCommand(fixture.commands, "SELECT"));

    RedisValueResult<std::string> get_result = client.get("name");
    assert(get_result.ok());
    assert(get_result.value == "libhv");

    RedisValueResult<int64_t> exists_result = client.exists("name");
    assert(exists_result.ok());
    assert(exists_result.value == 1);

    RedisValueResult<int64_t> expire_result = client.expire("name", 60);
    assert(expire_result.ok());
    assert(expire_result.value == 1);

    RedisValueResult<int64_t> hset_result = client.hset("user:1", "lang", "cpp");
    assert(hset_result.ok());
    assert(hset_result.value == 1);

    RedisValueResult<std::string> hget_result = client.hget("user:1", "lang");
    assert(hget_result.ok());
    assert(hget_result.value == "cpp");

    RedisValueResult<int64_t> publish_result = client.publish("updates", "hello");
    assert(publish_result.ok());
    assert(publish_result.value == 1);
    assert(fixture.published_messages.size() == 1);
    assert(fixture.published_messages[0] == "updates:hello");

    RedisResult commandf_result = client.commandf("SET %s %s", "language", "cxx");
    assert(commandf_result.ok());
    RedisValueResult<std::string> commandf_get = client.get("language");
    assert(commandf_get.ok());
    assert(commandf_get.value == "cxx");

    RedisValueResult<int64_t> del_result = client.del("name");
    assert(del_result.ok());
    assert(del_result.value == 1);

    RedisValueResult<std::string> missing_result = client.get("name");
    assert(!missing_result.ok());
    assert(missing_result.isNil());

    RedisResult unsupported = client.command(RedisCommand{"NOPE"});
    assert(unsupported.code == 0);
    assert(!unsupported.ok());
    assert(unsupported.reply.isError());
}

static void test_async_helpers() {
    RedisFixture fixture;
    RedisClient client;
    setupClient(&client, fixture.server.port());

    std::mutex mutex;
    std::condition_variable cv;
    int completed = 0;
    int expected = 0;
    bool all_ok = true;

    auto done = [&](bool ok) {
        std::lock_guard<std::mutex> lock(mutex);
        all_ok = all_ok && ok;
        ++completed;
        cv.notify_one();
    };

    int rc = client.setAsync("async:key", "value", [&](const RedisResult& result) {
        done(result.ok() && result.reply.asString() == "OK");
    });
    assert(rc == 0);
    ++expected;

    rc = client.getAsync("async:key", [&](const RedisValueResult<std::string>& result) {
        done(result.ok() && result.value == "value");
    });
    assert(rc == 0);
    ++expected;

    rc = client.existsAsync("async:key", [&](const RedisValueResult<int64_t>& result) {
        done(result.ok() && result.value == 1);
    });
    assert(rc == 0);
    ++expected;

    rc = client.expireAsync("async:key", 10, [&](const RedisValueResult<int64_t>& result) {
        done(result.ok() && result.value == 1);
    });
    assert(rc == 0);
    ++expected;

    rc = client.hsetAsync("user:2", "role", "tester", [&](const RedisValueResult<int64_t>& result) {
        done(result.ok() && result.value == 1);
    });
    assert(rc == 0);
    ++expected;

    rc = client.hgetAsync("user:2", "role", [&](const RedisValueResult<std::string>& result) {
        done(result.ok() && result.value == "tester");
    });
    assert(rc == 0);
    ++expected;

    rc = client.commandfAsync("SET %s %s", [&](const RedisResult& result) {
        done(result.ok() && result.reply.asString() == "OK");
    }, "fmt:key", "fmt-value");
    assert(rc == 0);
    ++expected;

    rc = client.getAsync("missing:key", [&](const RedisValueResult<std::string>& result) {
        done(result.isNil());
    });
    assert(rc == 0);
    ++expected;

    rc = client.commandAsync(RedisCommand{"NOPE"}, [&](const RedisResult& result) {
        done(result.code == 0 && result.reply.isError());
    });
    assert(rc == 0);
    ++expected;

    rc = client.delAsync("async:key", [&](const RedisValueResult<int64_t>& result) {
        done(result.ok() && result.value == 1);
    });
    assert(rc == 0);
    ++expected;

    rc = client.publishAsync("events", "async", [&](const RedisValueResult<int64_t>& result) {
        done(result.ok() && result.value == 1);
    });
    assert(rc == 0);
    ++expected;

    std::unique_lock<std::mutex> lock(mutex);
    bool ready = cv.wait_for(lock, std::chrono::seconds(3), [&completed, &expected]() { return completed == expected; });
    assert(ready);
    assert(all_ok);
    assert(hasCommand(fixture.commands, "AUTH"));
    assert(hasCommand(fixture.commands, "SELECT"));
    RedisValueResult<std::string> fmt_result = client.get("fmt:key");
    assert(fmt_result.ok());
    assert(fmt_result.value == "fmt-value");
    assert(fixture.published_messages.size() == 1);
    assert(fixture.published_messages[0] == "events:async");
}

static void test_sync_command_in_async_callback_is_rejected() {
    RedisFixture fixture;
    RedisClient client;
    setupClient(&client, fixture.server.port());
    assert(client.set("nested:key", "nested-value").ok());

    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    RedisValueResult<std::string> nested_result;

    int rc = client.getAsync("nested:key", [&](const RedisValueResult<std::string>& result) {
        assert(result.ok());
        RedisValueResult<std::string> local = client.get("nested:key");
        std::lock_guard<std::mutex> lock(mutex);
        nested_result = local;
        done = true;
        cv.notify_one();
    });
    assert(rc == 0);

    std::unique_lock<std::mutex> lock(mutex);
    bool ready = cv.wait_for(lock, std::chrono::seconds(3), [&done]() { return done; });
    assert(ready);
    assert(nested_result.code == ERR_INVALID_HANDLE);
    assert(!nested_result.ok());
}

int main() {
    test_sync_command_and_helpers();
    test_async_helpers();
    test_sync_command_in_async_callback_is_rejected();
    return 0;
}
