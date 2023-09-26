HTTP 服务端类

```c++

// HTTP服务类
class HttpServer {

    // 注册HTTP业务类
    void registerHttpService(HttpService* service);

    // 设置监听主机
    void setHost(const char* host = "0.0.0.0");
    // 设置监听端口
    void setPort(int port = 0, int ssl_port = 0);
    // 设置监听文件描述符
    void setListenFD(int fd = -1, int ssl_fd = -1);

    // 设置IO进程数 (仅`linux`下有效)
    void setProcessNum(int num);
    // 设置IO线程数
    void setThreadNum(int num);

    // 设置SSL/TLS
    int setSslCtx(hssl_ctx_t ssl_ctx);
    // 新建SSL/TLS
    int newSslCtx(hssl_ctx_opt_t* opt);

    // hooks
    // 事件循环开始时执行的回调函数
    std::function<void()> onWorkerStart;
    // 事件循环结束时执行的回调函数
    std::function<void()> onWorkerStop;

    // 占用当前线程运行
    int run(bool wait = true);

    // 不占用当前线程运行
    int start();

    // 停止服务
    int stop();

};

// HTTP业务类
class HttpService {

    // 添加静态资源映射
    void Static(const char* path, const char* dir);

    // 允许跨域访问
    void AllowCORS();

    // 添加可信代理 (代理白名单)
    void AddTrustProxy(const char* host);

    // 添加不可信代理 (代理黑名单)
    void AddNoProxy(const char* host);

    // 开启正向转发代理
    void EnableForwardProxy();

    // 添加反向代理映射
    void Proxy(const char* path, const char* url);

    // 添加中间件
    void Use(Handler handlerFunc);

    // 添加路由处理器
    void Handle(const char* httpMethod, const char* relativePath, Handler handlerFunc);

    // 添加`HEAD`路由
    void HEAD(const char* relativePath, Handler handlerFunc);

    // 添加`GET`路由
    void GET(const char* relativePath, Handler handlerFunc);

    // 添加`POST`路由
    void POST(const char* relativePath, Handler handlerFunc);

    // 添加`PUT`路由
    void PUT(const char* relativePath, Handler handlerFunc);

    // 添加`DELETE`路由
    void Delete(const char* relativePath, Handler handlerFunc);

    // 添加`PATCH`路由
    void PATCH(const char* relativePath, Handler handlerFunc);

    // 添加任意`HTTP method`路由
    void Any(const char* relativePath, Handler handlerFunc);

    // 返回注册的路由路径列表
    hv::StringList Paths();

    // 处理流程：前处理器 -> 中间件 -> 处理器 -> 后处理器
    // preprocessor -> middleware -> processor -> postprocessor

    // 数据成员
    http_handler    preprocessor;   // 前处理器
    http_handlers   middleware;     // 中间件
    http_handler    processor;      // 处理器
    http_handler    postprocessor;  // 后处理器
    std::string     base_url;       // 基本路径
    std::string     document_root;  // 文档根目录
    std::string     home_page;      // 主页
    std::string     error_page;     // 默认错误页
    std::string     index_of;       // 目录
    http_handler    errorHandler;   // 错误处理器

    int proxy_connect_timeout;      // 代理连接超时
    int proxy_read_timeout;         // 代理读超时
    int proxy_write_timeout;        // 代理写超时

    int keepalive_timeout;          // 长连接保活超时
    int max_file_cache_size;        // 文件缓存最大尺寸
    int file_cache_stat_interval;   // 文件缓存stat间隔，查询文件是否修改
    int file_cache_expired_time;    // 文件缓存过期时间，过期自动释放

    int limit_rate;                 // 下载速度限制

};

/* 几种`handler`处理函数区别说明: */

// 同步`handler`运行在IO线程
typedef std::function<int(HttpRequest* req, HttpResponse* resp)>                            http_sync_handler;

// 异步`handler`运行在`hv::async`全局线程池，可通过`hv::async::startup`设置线程池属性
typedef std::function<void(const HttpRequestPtr& req, const HttpResponseWriterPtr& writer)> http_async_handler;

// 上下文`handler`运行在IO线程，你可以很方便的将`HttpContextPtr`智能指针抛到你的消费者线程/线程池去处理
typedef std::function<int(const HttpContextPtr& ctx)>                                       http_ctx_handler;

// 中间状态`handler`运行在IO线程，用来实现大数据量的边接收边处理
typedef std::function<int(const HttpContextPtr& ctx, http_parser_state state, const char* data, size_t size)> http_state_handler;

```

测试代码见 [examples/http_server_test.cpp](../../examples/http_server_test.cpp)
