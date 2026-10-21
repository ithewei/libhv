SMTP 邮件发送客户端

基于事件循环的异步 SMTP 客户端，支持直连 TLS(SMTPS，如 465 端口)、多收件人/抄送、附件。

> 注意：只支持直连 TLS(SMTPS)，不支持 STARTTLS(587/143 明文中途升级)。主流邮箱(QQ/163/Gmail 等)均提供 465 直连 SSL 端口。

编译需开启 mail 模块与 SSL：

```sh
./configure --with-mail --with-openssl && make libhv
# 或 CMake: cmake .. -DWITH_MAIL=ON -DWITH_OPENSSL=ON
```

## C++ 接口

```c++
namespace hv {

class SmtpClient {
public:
    SmtpClient(hloop_t* loop = NULL);

    // 设置服务器: host、端口(默认465)、是否直连TLS(默认true)
    void setHost(const char* host, int port = 465, bool ssl = true);
    // 设置认证账号
    void setAuth(const char* username, const char* password);
    // 设置连接超时(ms)
    void setConnectTimeout(int ms);
    // SSL/TLS
    int setSslCtx(hssl_ctx_t ssl_ctx);
    int newSslCtx(hssl_ctx_opt_t* opt);

    // 结果回调: code∈[200,300) 表示成功; code<0 为 libhv ERR_*
    std::function<void(SmtpClient*, int code, const std::string& msg)> onResult;

    // 异步发送: 连接 -> EHLO -> AUTH -> MAIL FROM -> RCPT TO -> DATA -> QUIT
    int send(mail_t* mail);

    void run();   // 占用当前线程运行事件循环
    void stop();
};

}
```

## 邮件结构 mail_t

见 `mail/mime.h`，提供构造辅助函数(均为拷贝语义，用 `mail_clear` 释放)：

```c
void mail_set_from(mail_t* mail, const char* addr, const char* name);
void mail_add_to  (mail_t* mail, const char* addr, const char* name);
void mail_add_cc  (mail_t* mail, const char* addr, const char* name);
void mail_set_subject(mail_t* mail, const char* subject);
void mail_set_text(mail_t* mail, const char* text);   // text/plain
void mail_set_html(mail_t* mail, const char* html);   // text/html
void mail_add_attachment(mail_t* mail, const char* filename,
                         const char* content_type,      // NULL 则按扩展名推断
                         const void* data, size_t size);
void mail_clear(mail_t* mail);
```

## 示例

```c++
#include "smtp_client.h"
using namespace hv;

int main() {
    mail_t mail;
    memset(&mail, 0, sizeof(mail));
    mail_set_from(&mail, "you@qq.com", NULL);
    mail_add_to(&mail, "friend@example.com", NULL);
    mail_set_subject(&mail, "hello from libhv");
    mail_set_text(&mail, "This is a test mail.\r\n");

    SmtpClient cli;
    cli.setHost("smtp.qq.com", 465, true);
    cli.setAuth("you@qq.com", "yourpassword");  // 多数邮箱这里填授权码
    cli.onResult = [&cli](SmtpClient*, int code, const std::string& msg) {
        printf("result: %d %s\n", code, msg.c_str());
        cli.stop();
    };
    cli.send(&mail);
    cli.run();
    mail_clear(&mail);
    return 0;
}
```

测试代码见 [examples/mail/sendmail_test.cpp](../../examples/mail/sendmail_test.cpp)
