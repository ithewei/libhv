```c++

class HttpMessage {
    // 设置/获取头部
    void SetHeader(const char* key, const std::string& value);
    std::string GetHeader(const char* key, const std::string& defvalue = hv::empty_string);

    // 添加/获取cookie
    void AddCookie(const HttpCookie& cookie);
    const HttpCookie& GetCookie(const std::string& name);

    // 设置/获取 `Content-Type`
    void SetContentType(http_content_type type);
    http_content_type ContentType();

    // 获取 `Content-Length`
    size_t ContentLength();

    // 填充数据
    void SetBody(const std::string& body);
    // 获取数据
    const std::string& Body();
    // 解析数据
    int  ParseBody();

    // 填充/获取 `application/json` 格式数据
    template<typename T>
    int Json(const T& t);
    const hv::Json& GetJson();

    // 填充/获取 `multipart/form-data` 格式数据
    template<typename T>
    void SetFormData(const char* name, const T& t);
    void SetFormFile(const char* name, const char* filepath);
    std::string GetFormData(const char* name, const std::string& defvalue = hv::empty_string);
    int SaveFormFile(const char* name, const char* path);

    // 填充/获取 `application/x-www-urlencoded` 格式数据
    template<typename T>
    void SetUrlEncoded(const char* key, const T& t);
    std::string GetUrlEncoded(const char* key, const std::string& defvalue = hv::empty_string);

    // 根据 `Content-Type` 填充对应格式数据
    template<typename T>
    void Set(const char* key, const T& value);
    // 根据 `Content-Type` 获取对应格式数据
    template<typename T>
    T Get(const char* key, T defvalue = 0);
    // 根据 `Content-Type` 获取对应格式数据并转换成字符串
    std::string GetString(const char* key, const std::string& = "");
    // 根据 `Content-Type` 获取对应格式数据并转换成Boolean类型
    bool GetBool(const char* key, bool defvalue = 0);
    // 根据 `Content-Type` 获取对应格式数据并转换成整型
    int64_t GetInt(const char* key, int64_t defvalue = 0);
    // 根据 `Content-Type` 获取对应格式数据并转换成浮点数
    double GetFloat(const char* key, double defvalue = 0);
};

// HttpRequest 继承自 HttpMessage
class HttpRequest : public HttpMessage {
    // 设置/获取method
    void SetMethod(const char* method);
    const char* Method();

    // 设置URL
    void SetUrl(const char* url);
    // 获取URL
    const std::string& Url();
    // 解析URL
    void ParseUrl();
    // 获取Host
    std::string Host();
    // 获取Path
    std::string Path();

    // 设置/获取参数
    template<typename T>
    void SetParam(const char* key, const T& t);
    std::string GetParam(const char* key, const std::string& defvalue = hv::empty_string);

    // 设置代理
    void SetProxy(const char* host, int port);

    // 设置认证
    void SetAuth(const std::string& auth);
    void SetBasicAuth(const std::string& username, const std::string& password);
    void SetBearerTokenAuth(const std::string& token);

    // 设置请求超时
    void SetTimeout(int sec);
    // 设置连接超时
    void SetConnectTimeout(int sec);
    // 允许重定向
    void AllowRedirect(bool on = true);
    // 设置重试
    void SetRetry(int count = DEFAULT_HTTP_FAIL_RETRY_COUNT,
                  int delay = DEFAULT_HTTP_FAIL_RETRY_DELAY);
    // 取消
    void Cancel();
};

// HttpResponse 继承自 HttpMessage
class HttpResponse : public HttpMessage {
    // 状态码
    http_status status_code;
    // 状态字符串
    const char* status_message();
};

```
