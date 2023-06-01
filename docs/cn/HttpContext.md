```c++

class HttpContext {

    /* 获取请求信息 */
    // 获取客户端IP
    std::string ip();
    // 获取客户端端口
    int port();
    // 获取请求method
    http_method method();
    // 获取请求url
    std::string url();
    // 获取请求path
    std::string path();
    // 获取请求host
    std::string host();
    // 获取请求头部
    const http_headers& headers();
    std::string header(const char* key, const std::string& defvalue = hv::empty_string);
    // 获取请求参数
    const hv::QueryParams& params();
    std::string param(const char* key, const std::string& defvalue = hv::empty_string);
    // 获取请求cookie
    const HttpCookie& cookie(const char* name);
    // 获取请求 `Content-Length`
    int length();
    // 获取请求 `Content-Type`
    http_content_type type();
    // 判断请求 `Content-Type`
    bool is(http_content_type content_type);
    // 获取请求body
    std::string& body();
    // 获取 `application/json` 格式数据
    const hv::Json& json();
    // 获取 `multipart/form-data` 格式数据
    const hv::MultiPart& form();
    std::string form(const char* name, const std::string& defvalue = hv::empty_string);
    // 获取 `application/x-www-urlencoded` 格式数据
    const hv::KeyValue& urlencoded();
    std::string urlencoded(const char* key, const std::string& defvalue = hv::empty_string);
    // 根据 `Content-Type` 获取对应格式数据
    template<typename T>
    T get(const char* key, T defvalue = 0);
    std::string get(const char* key, const std::string& defvalue = hv::empty_string);

    /* 设置响应信息 */
    // 设置响应状态码
    void setStatus(http_status status);
    // 设置响应 `Content-Type`
    void setContentType(http_content_type type);
    // 设置响应头部
    void setHeader(const char* key, const std::string& value);
    // 设置响应cookie
    void setCookie(const HttpCookie& cookie);
    // 设置响应body
    void setBody(const std::string& body);
    template<typename T>
    // 根据 `Content-Type` 设置对应格式数据
    void set(const char* key, const T& value);

    // 发送
    int send();
    int send(const std::string& str, http_content_type type = APPLICATION_JSON);
    // 发送文本数据
    int sendString(const std::string& str);
    // 发送二进制数据
    int sendData(void* data, int len, bool nocopy = true);
    // 发送文件
    int sendFile(const char* filepath);
    // 发送json数据
    template<typename T>
    int sendJson(const T& t);

    // 重定向
    int redirect(const std::string& location, http_status status = HTTP_STATUS_FOUND);

    // 主动关闭连接
    int close();

};

```
