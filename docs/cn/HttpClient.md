HTTP 客户端类

```c++

class HttpClient {

    // 设置超时
    int setTimeout(int timeout);

    // 设置SSL/TLS
    int setSslCtx(hssl_ctx_t ssl_ctx);
    // 新建SSL/TLS
    int newSslCtx(hssl_ctx_opt_t* opt);

    // 清除全部请求头部
    int clearHeaders();
    // 设置请求头部
    int setHeader(const char* key, const char* value);
    // 删除请求头部
    int delHeader(const char* key);
    // 获取请求头部
    const char* getHeader(const char* key);

    // 设置http代理
    int setHttpProxy(const char* host, int port);
    // 设置https代理
    int setHttpsProxy(const char* host, int port);
    // 添加不走代理
    int addNoProxy(const char* host);

    // 同步发送
    int send(HttpRequest* req, HttpResponse* resp);
    // 异步发送
    int sendAsync(HttpRequestPtr req, HttpResponseCallback resp_cb = NULL);

    // 关闭连接 (HttpClient对象析构时会自动调用)
    int close();

};

namespace requests {

    // 同步请求
    Response request(Request req);
    Response request(http_method method, const char* url, const http_body& body = NoBody, const http_headers& headers = DefaultHeaders);

    // 上传文件
    Response uploadFile(const char* url, const char* filepath, http_method method = HTTP_POST, const http_headers& headers = DefaultHeaders);

    // 通过 `multipart/form-data` 格式上传文件
    Response uploadFormFile(const char* url, const char* name, const char* filepath, std::map<std::string, std::string>& params = hv::empty_map, http_method method = HTTP_POST, const http_headers& headers = DefaultHeaders);

    // 上传大文件（带上传进度回调）
    Response uploadLargeFile(const char* url, const char* filepath, upload_progress_cb progress_cb = NULL, http_method method = HTTP_POST, const http_headers& headers = DefaultHeaders);

    // 下载文件 (更详细的断点续传示例代码见`examples/wget.cpp`)
    size_t downloadFile(const char* url, const char* filepath, download_progress_cb progress_cb = NULL);

    // HEAD 请求
    Response head(const char* url, const http_headers& headers = DefaultHeaders);

    // GET 请求
    Response get(const char* url, const http_headers& headers = DefaultHeaders);

    // POST 请求
    Response post(const char* url, const http_body& body = NoBody, const http_headers& headers = DefaultHeaders);

    // PUT 请求
    Response put(const char* url, const http_body& body = NoBody, const http_headers& headers = DefaultHeaders);

    // PATCH 请求
    Response patch(const char* url, const http_body& body = NoBody, const http_headers& headers = DefaultHeaders);

    // DELETE 请求
    Response Delete(const char* url, const http_headers& headers = DefaultHeaders);

    // 异步请求
    int async(Request req, ResponseCallback resp_cb);

}

```

测试代码见 [examples/http_client_test.cpp](../../examples/http_client_test.cpp)
