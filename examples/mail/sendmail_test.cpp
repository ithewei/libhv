/*
 * sample SMTP mail sender
 *
 * @build   ./configure --with-mail --with-openssl && make libhv && make sendmail_test
 *          (or) make WITH_MAIL=yes WITH_OPENSSL=yes
 *
 * @run     bin/sendmail_test <smtp_host> <port> <username> <password> <from> <to>
 *
 * @example bin/sendmail_test smtp.qq.com 465 you@qq.com yourpass you@qq.com friend@example.com
 *
 * NOTE: only direct TLS (SMTPS, e.g. port 465) is supported, not STARTTLS.
 */

#include "smtp_client.h"
#include <stdio.h>

using namespace hv;

int main(int argc, char** argv) {
    if (argc < 7) {
        printf("Usage: %s smtp_host port username password from to\n", argv[0]);
        return -1;
    }
    const char* host = argv[1];
    int port = atoi(argv[2]);
    const char* username = argv[3];
    const char* password = argv[4];
    const char* from = argv[5];
    const char* to = argv[6];

    // build the mail
    mail_t mail;
    memset(&mail, 0, sizeof(mail));
    mail_set_from(&mail, from, NULL);
    mail_add_to(&mail, to, NULL);
    mail_set_subject(&mail, "hello from libhv");
    mail_set_text(&mail, "This is a test mail sent by libhv SmtpClient.\r\n");
    // optional attachment:
    // const char* content = "attachment content";
    // mail_add_attachment(&mail, "hello.txt", NULL, content, strlen(content));

    SmtpClient cli;
    cli.setHost(host, port, port == 465 /* ssl for 465 */);
    cli.setAuth(username, password);
    cli.setConnectTimeout(10000);
    cli.onResult = [&cli](SmtpClient*, int code, const std::string& msg) {
        if (code >= 200 && code < 300) {
            printf("send mail success: %d %s\n", code, msg.c_str());
        } else {
            printf("send mail failed: %d %s\n", code, msg.c_str());
        }
        cli.stop();
    };

    int ret = cli.send(&mail);
    if (ret != 0) {
        printf("send start failed: %d\n", ret);
        mail_clear(&mail);
        return ret;
    }
    cli.run();   // block until stopped
    mail_clear(&mail);
    return 0;
}
