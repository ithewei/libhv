#include <stdio.h>

#include "smtp.h"

int main(int argc, char** argv) {
    if (argc < 8) {
        printf("Usage: sendmail smtp_server username password from to subject body\n");
        return -10;
    }

    const char* smtp_server = argv[1];
    const char* username = argv[2];
    const char* password = argv[3];
    mail_t mail;
    mail.from = argv[4];
    mail.to = argv[5];
    mail.subject = argv[6];
    mail.body = argv[7];

    int status_code = sendmail(smtp_server, username, password, &mail);
    printf("sendmail: %d %s\n", status_code, smtp_status_str((enum smtp_status)status_code));

    return status_code == SMTP_STATUS_OK ? 0 : status_code;
}
