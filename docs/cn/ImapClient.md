IMAP 邮件接收客户端

基于事件循环的异步 IMAP 客户端，支持直连 TLS(IMAPS，如 993 端口)，读取邮件(读取最小集：LOGIN / SELECT / SEARCH / FETCH / LOGOUT)，并解析 MIME 报文得到发件人、主题、正文与附件。

> 注意：
> - 只支持直连 TLS(IMAPS)，不支持 STARTTLS。主流邮箱(QQ/163/Gmail 等)均提供 993 直连 SSL 端口。
> - 只实现读取相关命令，不支持写操作(STORE 改标记 / COPY / 移动 / IDLE 推送等)。

编译需开启 mail 模块与 SSL：

```sh
./configure --with-mail --with-openssl && make libhv
# 或 CMake: cmake .. -DWITH_MAIL=ON -DWITH_OPENSSL=ON
```

## C++ 接口

```c++
namespace hv {

class ImapClient {
public:
    ImapClient(hloop_t* loop = NULL);

    // 设置服务器: host、端口(默认993)、是否直连TLS(默认true)
    void setHost(const char* host, int port = 993, bool ssl = true);
    // 设置认证账号
    void setAuth(const char* username, const char* password);
    // 设置连接超时(ms)
    void setConnectTimeout(int ms);
    // SSL/TLS
    int setSslCtx(hssl_ctx_t ssl_ctx);
    int newSslCtx(hssl_ctx_opt_t* opt);

    // 每封邮件回调: mail 在回调期间有效
    std::function<void(ImapClient*, mail_t*)> onMail;
    // 完成/失败回调: code==0 成功; code<0 为 libhv ERR_*
    std::function<void(ImapClient*, int code, const std::string& msg)> onDone;

    // 拉取: LOGIN -> SELECT mailbox -> SEARCH criteria -> 逐封 FETCH -> LOGOUT
    // mailbox 如 "INBOX"; criteria 为 IMAP SEARCH 条件，如 "ALL"、"UNSEEN"
    int fetch(const char* mailbox = "INBOX", const char* criteria = "ALL");

    void run();   // 占用当前线程运行事件循环
    void stop();
};

}
```

## 示例

```c++
#include "imap_client.h"
using namespace hv;

int main() {
    ImapClient cli;
    cli.setHost("imap.qq.com", 993, true);
    cli.setAuth("you@qq.com", "yourpassword");  // 多数邮箱这里填授权码
    cli.onMail = [](ImapClient*, mail_t* mail) {
        printf("From: %s\n", mail->from.addr ? mail->from.addr : "");
        printf("Subject: %s\n", mail->subject ? mail->subject : "");
        if (mail->text_body) printf("Body: %s\n", mail->text_body);
    };
    cli.onDone = [&cli](ImapClient*, int code, const std::string& msg) {
        printf("done: %d %s\n", code, msg.c_str());
        cli.stop();
    };
    cli.fetch("INBOX", "UNSEEN");   // 拉取未读邮件
    cli.run();
    return 0;
}
```

测试代码见 [examples/mail/recvmail_test.cpp](../../examples/mail/recvmail_test.cpp)
