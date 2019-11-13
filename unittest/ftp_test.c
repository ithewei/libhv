#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ftp.h"

void print_help() {
    printf("Usage:\n\
help\n\
login <username> <password>\n\
download <remote_filepath> <local_filepath>\n\
upload   <local_filepath>  <remote_filepath>\n\
quit\n");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: ftp host [port]\n");
        return 0;
    }
    const char* host = argv[1];
    int port = FTP_COMMAND_PORT;
    if (argc >= 3) {
        port = atoi(argv[2]);
    }
    int ret = 0;
    ftp_handle_t hftp;
    ret = ftp_connect(&hftp, host, port);
    if (ret != 0) {
        printf("ftp connect failed!\n");
        return ret;
    }
    print_help();

    char cmd[256] = {0};
    char param1[256] = {0};
    char param2[256] = {0};
    while (1) {
        printf("> ");
        scanf("%s", cmd);
        if (strncmp(cmd, "help", 4) == 0) {
            print_help();
        }
        else if (strncmp(cmd, "login", 5) == 0) {
            scanf("%s", param1);
            scanf("%s", param2);
            //printf("cmd=%s param1=%s param2=%s\n", cmd, param1, param2);
            const char* username = param1;
            const char* password = param2;
            ret = ftp_login(&hftp, username, password);
            printf("%s", hftp.recvbuf);
            if (ret != 0) break;
        }
        else if (strncmp(cmd, "upload", 6) == 0) {
            scanf("%s", param1);
            scanf("%s", param2);
            //printf("cmd=%s param1=%s param2=%s\n", cmd, param1, param2);
            const char* localfile = param1;
            const char* remotefile = param2;
            ret = ftp_upload(&hftp, localfile, remotefile);
            printf("%s", hftp.recvbuf);
            if (ret != 0) break;
        }
        else if (strncmp(cmd, "download", 8) == 0) {
            scanf("%s", param1);
            scanf("%s", param2);
            //printf("cmd=%s param1=%s param2=%s\n", cmd, param1, param2);
            const char* remotefile = param1;
            const char* localfile = param2;
            ret = ftp_download(&hftp, remotefile, localfile);
            printf("%s", hftp.recvbuf);
            if (ret != 0) break;
        }
        else if (strncmp(cmd, "quit", 4) == 0) {
            break;
        }
        else {
            scanf("%s", param1);
            //printf("cmd=%s param=%s\n", cmd, param1);
            ret = ftp_exec(&hftp, cmd, param1);
            printf("%s", hftp.recvbuf);
        }
    }
    printf("QUIT\n");
    ftp_quit(&hftp);
    printf("%s", hftp.recvbuf);
    return 0;
}
