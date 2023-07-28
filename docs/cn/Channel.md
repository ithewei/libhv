通道类

```c++

class Channel {

    // 返回底层的io结构体指针
    hio_t*      io() { return io_; }

    // 返回socket文件描述符
    int         fd() { return fd_; }

    // 返回一个唯一标示id
    uint32_t    id() { return id_; }

    // 返回错误码
    int error() { return hio_error(io_); }

    // 获取/设置/新建/删除 上下文
    void* context();
    void setContext(void* ctx);
    template<class T> T* newContext();
    template<class T> T* getContext();
    template<class T> void deleteContext();

    // 获取/设置/新建/删除 上下文智能指针
    std::shared_ptr<void> contextPtr();
    void setContextPtr(const std::shared_ptr<void>& ctx);
    void setContextPtr(std::shared_ptr<void>&& ctx);
    template<class T> std::shared_ptr<T> newContextPtr();
    template<class T> std::shared_ptr<T> getContextPtr();
    void deleteContextPtr();

    // 是否打开状态
    bool isOpened();

    // 是否关闭状态
    bool isClosed();

    // 开始读
    int startRead();

    // 停止读
    int stopRead();

    // 读一次
    int readOnce();
    // 读一个字符串
    int readString();
    // 读一行
    int readLine();
    // 读取N个字节
    int readBytes(int len);

    // 写
    int write(const void* data, int size);
    int write(Buffer* buf);
    int write(const std::string& str);

    // 设置最大读缓存
    void setMaxReadBufsize(uint32_t size);
    // 设置最大写缓存
    void setMaxWriteBufsize(uint32_t size);
    // 获取当前写缓存大小
    size_t writeBufsize();
    // 是否写完成
    bool isWriteComplete();

    // 关闭
    int close(bool async = false);

    // 读回调
    std::function<void(Buffer*)> onread;
    // 写回调
    std::function<void(Buffer*)> onwrite;
    // 关闭回调
    std::function<void()>        onclose;
};

// SocketChannel 继承自 Channel
class SocketChannel : public Channel {
    // 连接状态回调
    std::function<void()>   onconnect;
    // 心跳回调
    std::function<void()>   heartbeat;

    // 启用SSL/TLS加密通信
    int enableSSL();
    // 是否是SSL/TLS加密通信
    bool isSSL();
    // 设置SSL
    int setSSL(hssl_t ssl);
    // 设置SSL_CTX
    int setSslCtx(hssl_ctx_t ssl_ctx);
    // 新建SSL_CTX
    int newSslCtx(hssl_ctx_opt_t* opt);
    // 设置主机名
    int setHostname(const std::string& hostname);

    // 设置连接超时
    void setConnectTimeout(int timeout_ms);

    // 设置关闭超时 (说明：非阻塞写队列非空时，需要等待写完成再关闭)
    void setCloseTimeout(int timeout_ms);

    // 设置读超时 (一段时间没有数据到来便自动关闭连接)
    void setReadTimeout(int timeout_ms);

    // 设置写超时 (一段时间没有数据发送便自动关闭连接)
    void setWriteTimeout(int timeout_ms);

    // 设置keepalive超时 (一段时间没有数据收发便自动关闭连接)
    void setKeepaliveTimeout(int timeout_ms);

    // 设置心跳 (定时发送心跳包)
    void setHeartbeat(int interval_ms, std::function<void()> fn);

    // 设置拆包规则
    void setUnpack(unpack_setting_t* setting);

    // 开始连接
    int startConnect(int port, const char* host = "127.0.0.1");
    int startConnect(struct sockaddr* peeraddr);
    int startConnect();

    // 是否已连接
    bool isConnected();

    // 返回本地地址
    std::string localaddr();

    // 返回对端地址
    std::string peeraddr();
};

// WebSocketChannel 继承自 SocketChannel
class WebSocketChannel : public SocketChannel {

    // 发送文本帧
    int send(const std::string& msg, enum ws_opcode opcode = WS_OPCODE_TEXT, bool fin = true);

    // 发送二进制帧
    int send(const char* buf, int len, enum ws_opcode opcode = WS_OPCODE_BINARY, bool fin = true);

    // 分片发送
    int send(const char* buf, int len, int fragment, enum ws_opcode opcode = WS_OPCODE_BINARY);

    // 关闭
    int close();

};

```
