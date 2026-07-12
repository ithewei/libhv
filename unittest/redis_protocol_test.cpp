#include <assert.h>

#include <string>

#include "redis/RedisMessage.h"

using namespace hv;

static void test_encode_command() {
    RedisCommand cmd;
    cmd.push_back("SET");
    cmd.push_back("key");
    cmd.push_back("value");
    std::string wire = RedisEncodeCommand(cmd);
    assert(wire == "*3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$5\r\nvalue\r\n");
}

static void test_encode_reply() {
    RedisReply simple;
    simple.type = REDIS_REPLY_STRING;
    simple.str = "OK";
    assert(RedisEncodeReply(simple) == "+OK\r\n");

    RedisReply bulk;
    bulk.type = REDIS_REPLY_STRING;
    bulk.str = "value";
    bulk.bulk = true;
    assert(RedisEncodeReply(bulk) == "$5\r\nvalue\r\n");

    RedisReply nil;
    nil.type = REDIS_REPLY_NIL;
    assert(RedisEncodeReply(nil) == "$-1\r\n");

    RedisReply integer;
    integer.type = REDIS_REPLY_INTEGER;
    integer.integer = 42;
    assert(RedisEncodeReply(integer) == ":42\r\n");

    RedisReply array;
    array.type = REDIS_REPLY_ARRAY;
    array.elements.push_back(simple);
    array.elements.push_back(integer);
    assert(RedisEncodeReply(array) == "*2\r\n+OK\r\n:42\r\n");
}

static void test_result_ok_semantics() {
    RedisResult success;
    success.code = 0;
    assert(success.ok());

    RedisResult client_error;
    client_error.code = -1;
    assert(!client_error.ok());

    RedisResult redis_error;
    redis_error.code = 0;
    redis_error.reply.type = REDIS_REPLY_ERROR;
    redis_error.reply.str = "ERR wrongtype";
    assert(!redis_error.ok());
}

static void test_parse_fragmented_replies() {
    RedisParser parser;
    std::string payload = "+OK\r\n$-1\r\n*2\r\n:1\r\n-ERR wrongtype\r\n";
    parser.Feed(payload.data(), 7);
    assert(parser.HasReply());
    RedisReply ok = parser.NextReply();
    assert(ok.type == REDIS_REPLY_STRING);
    assert(ok.str == "OK");
    assert(!ok.bulk);

    parser.Feed(payload.data() + 7, payload.size() - 7);
    RedisReply nil = parser.NextReply();
    assert(nil.type == REDIS_REPLY_NIL);
    RedisReply array = parser.NextReply();
    assert(array.type == REDIS_REPLY_ARRAY);
    assert(array.elements.size() == 2);
    assert(array.elements[0].type == REDIS_REPLY_INTEGER);
    assert(array.elements[0].integer == 1);
    assert(array.elements[1].type == REDIS_REPLY_ERROR);
    assert(array.elements[1].str == "ERR wrongtype");
}

static void test_parse_nested_array_and_bulk_string() {
    RedisParser parser;
    std::string payload = "*3\r\n$5\r\nvalue\r\n$0\r\n\r\n*2\r\n:7\r\n*1\r\n$-1\r\n";
    parser.Feed(payload.data(), payload.size());
    assert(parser.HasReply());
    RedisReply reply = parser.NextReply();
    assert(reply.type == REDIS_REPLY_ARRAY);
    assert(reply.elements.size() == 3);
    assert(reply.elements[0].type == REDIS_REPLY_STRING);
    assert(reply.elements[0].bulk);
    assert(reply.elements[0].str == "value");
    assert(reply.elements[1].type == REDIS_REPLY_STRING);
    assert(reply.elements[1].bulk);
    assert(reply.elements[1].str.empty());
    assert(reply.elements[2].type == REDIS_REPLY_ARRAY);
    assert(reply.elements[2].elements.size() == 2);
    assert(reply.elements[2].elements[0].type == REDIS_REPLY_INTEGER);
    assert(reply.elements[2].elements[0].integer == 7);
    assert(reply.elements[2].elements[1].type == REDIS_REPLY_ARRAY);
    assert(reply.elements[2].elements[1].elements.size() == 1);
    assert(reply.elements[2].elements[1].elements[0].type == REDIS_REPLY_NIL);
    assert(!reply.elements[2].elements[1].elements[0].null_array);
}

static void test_null_array_round_trip() {
    RedisParser parser;
    std::string payload = "*-1\r\n";
    parser.Feed(payload.data(), payload.size());
    assert(parser.HasReply());
    RedisReply reply = parser.NextReply();
    assert(reply.type == REDIS_REPLY_NIL);
    assert(reply.null_array);
    assert(RedisEncodeReply(reply) == "*-1\r\n");
}

static void test_parse_hard_error_does_not_block_following_reply() {
    RedisParser parser;
    std::string payload = "?bad\r\n+OK\r\n";
    parser.Feed(payload.data(), payload.size());
    assert(parser.HasError());
    assert(parser.HasReply());
    RedisReply reply = parser.NextReply();
    assert(reply.type == REDIS_REPLY_STRING);
    assert(reply.str == "OK");
}

static void test_parse_incomplete_does_not_set_error() {
    RedisParser parser;
    parser.Feed("$5\r\nva", 6);
    assert(!parser.HasError());
    assert(!parser.HasReply());

    parser.Feed("lue\r\n", 5);
    assert(!parser.HasError());
    assert(parser.HasReply());
    RedisReply reply = parser.NextReply();
    assert(reply.type == REDIS_REPLY_STRING);
    assert(reply.bulk);
    assert(reply.str == "value");
}

static void test_parse_hard_error_in_nested_array_recovers() {
    RedisParser parser;
    std::string payload = "*2\r\n:1\r\n$-2\r\n+NEXT\r\n";
    parser.Feed(payload.data(), payload.size());
    assert(parser.HasError());
    assert(parser.HasReply());
    RedisReply reply = parser.NextReply();
    assert(reply.type == REDIS_REPLY_STRING);
    assert(reply.str == "NEXT");
}

int main() {
    test_encode_command();
    test_encode_reply();
    test_result_ok_semantics();
    test_parse_fragmented_replies();
    test_parse_nested_array_and_bulk_string();
    test_null_array_round_trip();
    test_parse_hard_error_does_not_block_following_reply();
    test_parse_incomplete_does_not_set_error();
    test_parse_hard_error_in_nested_array_recovers();
    return 0;
}
