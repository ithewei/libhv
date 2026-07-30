#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "redis/RedisClient.h"

using namespace hv;

int main(int argc, char** argv) {
    std::string host = argc > 1 ? argv[1] : "127.0.0.1";
    int port = argc > 2 ? atoi(argv[2]) : 6379;

    RedisClient client;
    client.setHost(host);
    client.setPort(port);
    client.setConnectTimeout(3000);
    client.setTimeout(3000);

    RedisResult set_result = client.set("libhv:redis:example", "hello");
    if (!set_result.ok()) {
        std::cerr << "SET failed, code=" << set_result.code << std::endl;
        return set_result.code != 0 ? set_result.code : -1;
    }

    RedisValueResult<std::string> value = client.get("libhv:redis:example");
    if (!value.ok()) {
        std::cerr << "GET failed, code=" << value.code << std::endl;
        return value.code != 0 ? value.code : -1;
    }
    std::cout << "GET libhv:redis:example => " << value.value << std::endl;

    RedisPipeline pipeline = client.pipeline();
    pipeline.appendCommand(RedisCommand{"SET", "libhv:redis:pipeline", "1"});
    pipeline.appendCommand(RedisCommand{"GET", "libhv:redis:pipeline"});
    std::vector<RedisReply> replies;
    RedisResult pipe_result = pipeline.exec(&replies);
    if (!pipe_result.ok()) {
        std::cerr << "Pipeline failed, code=" << pipe_result.code << std::endl;
        return pipe_result.code != 0 ? pipe_result.code : -1;
    }
    std::cout << "Pipeline replies=" << replies.size() << std::endl;

    RedisTransaction tx = client.transaction();
    tx.appendCommand(RedisCommand{"SET", "libhv:redis:tx", "7"});
    tx.appendCommand(RedisCommand{"GET", "libhv:redis:tx"});
    replies.clear();
    RedisResult tx_result = tx.exec(&replies);
    if (!tx_result.ok()) {
        std::cerr << "Transaction failed, code=" << tx_result.code << std::endl;
        return tx_result.code != 0 ? tx_result.code : -1;
    }
    std::cout << "Transaction replies=" << replies.size() << std::endl;

    return 0;
}
