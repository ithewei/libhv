/*
 * sample IMAP mail receiver
 *
 * @build   ./configure --with-mail --with-openssl && make libhv && make recvmail_test
 *          (or) make WITH_MAIL=yes WITH_OPENSSL=yes
 *
 * @run     bin/recvmail_test <imap_host> <port> <username> <password> [mailbox] [criteria]
 *
 * @example bin/recvmail_test imap.qq.com 993 you@qq.com yourpass INBOX UNSEEN
 *
 * NOTE: only direct TLS (IMAPS, e.g. port 993) is supported, not STARTTLS.
 */

#include "imap_client.h"
#include <stdio.h>

using namespace hv;

int main(int argc, char** argv) {
    if (argc < 5) {
        printf("Usage: %s imap_host port username password [mailbox] [criteria]\n", argv[0]);
        return -1;
    }
    const char* host = argv[1];
    int port = atoi(argv[2]);
    const char* username = argv[3];
    const char* password = argv[4];
    const char* mailbox  = argc > 5 ? argv[5] : "INBOX";
    const char* criteria = argc > 6 ? argv[6] : "ALL";

    ImapClient cli;
    cli.setHost(host, port, port == 993 /* ssl for 993 */);
    cli.setAuth(username, password);
    cli.setConnectTimeout(10000);

    int count = 0;
    cli.onMail = [&count](ImapClient*, mail_t* mail) {
        ++count;
        printf("---- mail #%d ----\n", count);
        printf("From:    %s\n", mail->from.addr ? mail->from.addr : "");
        printf("Subject: %s\n", mail->subject ? mail->subject : "");
        printf("Date:    %s\n", mail->date ? mail->date : "");
        if (mail->text_body) {
            printf("Body:\n%.200s%s\n", mail->text_body,
                   strlen(mail->text_body) > 200 ? "..." : "");
        }
        if (mail->attachment_count > 0) {
            printf("Attachments: %d\n", mail->attachment_count);
            for (int i = 0; i < mail->attachment_count; ++i) {
                printf("  - %s (%zu bytes)\n",
                       mail->attachments[i].filename ? mail->attachments[i].filename : "?",
                       mail->attachments[i].size);
            }
        }
    };
    cli.onDone = [&cli, &count](ImapClient*, int code, const std::string& msg) {
        printf("---- done: %d mail(s), code=%d %s ----\n", count, code, msg.c_str());
        cli.stop();
    };

    int ret = cli.fetch(mailbox, criteria);
    if (ret != 0) {
        printf("fetch start failed: %d\n", ret);
        return ret;
    }
    cli.run();   // block until stopped
    return 0;
}
