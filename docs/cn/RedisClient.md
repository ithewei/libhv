Redis 客户端类

libhv 的 Redis C++ 模块位于仓库根目录 `redis/`，采用 `.h + .cpp` 分离结构。第一版只支持单机 Redis 与 RESP2 协议，提供同步 / 异步命令、typed helpers、pipeline、transaction 和 pub/sub。

Redis 模块默认不编译，需要显式开启：

```shell
./configure --with-redis && make libhv
# 或
cmake -S . -B build -DWITH_EVPP=ON -DWITH_REDIS=ON && cmake --build build
```

## 结果模型

命令的结果严格区分三类语义：客户端错误、Redis 服务端错误回复、nil / 空结果。

```c++
// RESP2 回复类型
enum RedisReplyType {
    REDIS_REPLY_NIL,        // nil
    REDIS_REPLY_STRING,     // 字符串 (SimpleString / BulkString)
    REDIS_REPLY_ERROR,      // 服务端错误回复
    REDIS_REPLY_INTEGER,    // 整数
    REDIS_REPLY_ARRAY,      // 数组
};

// 统一底层回复对象
struct RedisReply {
    RedisReplyType type;
    std::string    str;         // 字符串 / 错误信息
    int64_t        integer;     // 整数
    std::vector<RedisReply> elements;   // 数组元素

    bool isNil()    const;
    bool isError()  const;
    bool isArray()  const;
    bool isString() const;
    const std::string&           error()    const;   // 错误信息
    const std::string&           asString() const;
    int64_t                      asInt()    const;
    const std::vector<RedisReply>& asArray() const;
};

// 一次命令调用的统一结果
struct RedisResult {
    int        code;    // 0 表示客户端侧成功
    RedisReply reply;   // 客户端侧成功时保存 Redis 回复

    bool ok() const;    // code == 0 且 reply 不是错误回复
};

// typed helper 的结果 (带具体值 value)
template<typename T>
struct RedisValueResult {
    int        code;
    RedisReply reply;
    T          value;
    bool       has_value;

    bool ok()    const;    // code == 0 且非错误回复且 has_value
    bool isNil() const;    // code == 0 且回复为 nil (例如 GET 不存在的 key)
};
```

语义约定：

- `code == 0`：客户端侧调用成功，`reply` 有效。
- `code != 0`：客户端侧失败（未连接、连接失败、超时、断线、协议错误、参数非法等），`reply` 无效。
- `code == 0 && reply.isError()`：成功收到 Redis 服务端错误回复（如 `-ERR`、`-WRONGTYPE`、`-NOAUTH`）。
- `isNil()`：不是错误，例如 `GET` 不存在的 key。

## class RedisClient

面向多数使用者的主入口，同步 + 异步接口共存，用法与 `HttpClient` 类似。内部使用独立线程运行事件循环，同步接口在内部等待完成，对外无需操作 future。

```c++
class RedisClient {

    RedisClient();

    // 连接配置
    void setHost(const std::string& host);
    void setPort(int port);
    void setAuth(const std::string& password);   // AUTH
    void setDb(int db);                           // SELECT
    void setConnectTimeout(int ms);               // 连接超时
    void setTimeout(int ms);                      // 命令读写超时
    void setReconnect(reconn_setting_t* setting); // 断线重连

    // 原始命令 (参数列表风格, 主推, binary-safe)
    RedisResult command(const RedisCommand& command);              // 同步
    int commandAsync(const RedisCommand& command, RedisCallback cb);  // 异步

    // 原始命令 (format 风格, 兼容重载)
    RedisResult commandf(const char* fmt, ...);
    template<typename... Args>
    int commandfAsync(const char* fmt, RedisCallback cb, Args... args);

    // pipeline / transaction 工厂
    RedisPipeline    pipeline();
    RedisTransaction transaction();

    // typed helpers (同步 + 异步)
    RedisValueResult<std::string> get(const std::string& key);
    int getAsync(const std::string& key, RedisValueCallback<std::string> cb);

    RedisResult set(const std::string& key, const std::string& value);
    int setAsync(const std::string& key, const std::string& value, RedisCallback cb);

    RedisValueResult<int64_t> del(const std::string& key);
    RedisValueResult<int64_t> exists(const std::string& key);
    RedisValueResult<int64_t> expire(const std::string& key, int seconds);
    RedisValueResult<std::string> hget(const std::string& key, const std::string& field);
    RedisValueResult<int64_t> hset(const std::string& key, const std::string& field, const std::string& value);
    RedisValueResult<int64_t> publish(const std::string& channel, const std::string& message);
    // ...对应的 delAsync / existsAsync / expireAsync / hgetAsync / hsetAsync / publishAsync
};
```

其中 `RedisCommand` 即 `std::vector<std::string>`，`RedisCallback` 为 `std::function<void(const RedisResult&)>`。

typed helpers 是对原始命令的薄封装，复用统一的命令编码、reply 解析与错误语义，不构成第二套协议实现。

### 同步用法

```c++
using namespace hv;

RedisClient client;
client.setHost("127.0.0.1");
client.setPort(6379);
client.setConnectTimeout(3000);
client.setTimeout(3000);

// typed helper
if (client.set("key", "hello").ok()) {
    RedisValueResult<std::string> v = client.get("key");
    if (v.ok()) {
        printf("key => %s\n", v.value.c_str());
    } else if (v.isNil()) {
        printf("key not exists\n");
    }
}

// 原始命令
RedisResult r = client.command(RedisCommand{"INCR", "counter"});
if (r.ok()) {
    printf("counter = %lld\n", (long long)r.reply.asInt());
}
```

### 异步用法

异步命令采用单连接、FIFO 配对模型：按发送顺序取出对应 callback。回调保证只触发一次。

```c++
client.getAsync("key", [](const RedisValueResult<std::string>& v) {
    if (v.ok()) {
        printf("key => %s\n", v.value.c_str());
    }
});
```

> 注意：不要在异步回调线程里再调用同步接口（如 `client.get(...)`），会因处于事件循环线程内被拒绝并返回 `ERR_INVALID_HANDLE`。

## class RedisPipeline

批量命令对象：先累积命令，`exec` 时一次性发送，按顺序返回 N 条回复。整体失败（发送失败 / 超时 / 断线 / 回复不完整）通过 `RedisResult.code` 体现；单条命令的错误回复是 `replies` 中某一项的合法 error reply。

```c++
class RedisPipeline {
    void appendCommand(const RedisCommand& command);
    RedisResult exec(std::vector<RedisReply>* replies = NULL);  // 同步
    int execAsync(const RedisRepliesCallback& cb);              // 异步
};
```

```c++
RedisPipeline pipe = client.pipeline();
pipe.appendCommand(RedisCommand{"SET", "counter", "1"});
pipe.appendCommand(RedisCommand{"INCR", "counter"});

std::vector<RedisReply> replies;
RedisResult result = pipe.exec(&replies);
if (result.ok()) {
    printf("INCR => %lld\n", (long long)replies[1].asInt());  // 2
}
```

## class RedisTransaction

封装 `MULTI` / `EXEC` / `DISCARD`。`exec` 返回事务结果数组。第一版不把 `WATCH` 及其冲突重试作为重点能力。

```c++
class RedisTransaction {
    void appendCommand(const RedisCommand& command);
    RedisResult exec(std::vector<RedisReply>* replies = NULL);  // MULTI + 命令 + EXEC
    RedisResult discard();                                      // DISCARD
};
```

```c++
RedisTransaction tx = client.transaction();
tx.appendCommand(RedisCommand{"SET", "k", "7"});
tx.appendCommand(RedisCommand{"GET", "k"});

std::vector<RedisReply> replies;
RedisResult result = tx.exec(&replies);
if (result.ok()) {
    printf("GET => %s\n", replies[1].asString().c_str());  // 7
}
```

## class AsyncRedisClient

面向事件循环模型的纯异步客户端，风格贴近 `TcpClient` / `AsyncHttpClient`。`RedisClient` 的异步能力即基于它实现。适合高并发、长连接、批量发送场景。

```c++
class AsyncRedisClient {

    AsyncRedisClient(EventLoopPtr loop = NULL);

    // 连接配置 (同 RedisClient)
    void setHost(const std::string& host);
    void setPort(int port);
    void setAuth(const std::string& password);
    void setDb(int db);
    void setConnectTimeout(int ms);
    void setTimeout(int ms);
    void setReconnect(reconn_setting_t* setting);

    // 生命周期
    void start(bool wait_threads_started = true);
    void stop(bool wait_threads_stopped = true);
    bool isConnected() const;
    bool isStarted() const;
    bool isInLoopThread();

    // 异步命令
    int command(const RedisCommand& command, RedisCallback cb);
    int commandBatch(const std::vector<RedisCommand>& commands, RedisRepliesCallback cb);

    // 事件回调
    std::function<void()>    onConnect;
    std::function<void()>    onClose;
    std::function<void(int)> onError;
};
```

> 连接断开时，所有尚未完成的 pending 异步请求会统一以客户端错误失败，且不会自动重放未完成命令（Redis 命令可能有副作用，自动重放不安全）。

## class RedisSubscriber

独立订阅客户端，专门处理 Pub/Sub。使用独立连接，不与普通命令连接复用状态机。

```c++
class RedisSubscriber {

    RedisSubscriber(EventLoopPtr loop = NULL);

    // 连接配置
    void setHost(const std::string& host);
    void setPort(int port);
    void setAuth(const std::string& password);
    void setDb(int db);
    void setReconnect(reconn_setting_t* setting);

    // 生命周期
    void start(bool wait_threads_started = true);
    void stop(bool wait_threads_stopped = true);

    // 订阅 / 退订
    int subscribe(const std::string& channel);
    int psubscribe(const std::string& pattern);
    int unsubscribe(const std::string& channel);
    int punsubscribe(const std::string& pattern);

    // 事件回调
    std::function<void(const std::string& channel, const std::string& message)> onMessage;
    std::function<void(const std::string& name)> onSubscribe;
    std::function<void(const std::string& name)> onUnsubscribe;
    std::function<void(int code)>                onError;
};
```

```c++
RedisSubscriber subscriber;
subscriber.setHost("127.0.0.1");
subscriber.setPort(6379);
subscriber.onMessage = [](const std::string& channel, const std::string& message) {
    printf("%s => %s\n", channel.c_str(), message.c_str());
};
subscriber.start();
subscriber.subscribe("news");
```

> 配置了 reconnect 时，连接恢复后会重放当前订阅集合并继续接收推送；已经显式退订的条目不会自动恢复。

## 说明

- 第一版明确不做：Redis Sentinel、Redis Cluster、RESP3、coroutine / future-first 的公开 API、`WATCH` 冲突重试、自动重放未完成命令、大而全的全命令 typed API 覆盖。
- 普通命令流与订阅流分离，是第一版设计中的硬边界。

测试代码见 [examples/redis_client_test.cpp](../../examples/redis_client_test.cpp) 和 [examples/redis_subscriber_test.cpp](../../examples/redis_subscriber_test.cpp)
