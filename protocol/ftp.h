#ifndef HV_FTP_H_
#define HV_FTP_H_

#include "hexport.h"

#define FTP_COMMAND_PORT    21
#define FTP_DATA_PORT       20

// ftp_command
// X(name)
#define FTP_COMMAND_MAP(X) \
    X(HELP)     \
    X(USER)     \
    X(PASS)     \
    X(PWD)      \
    X(CWD)      \
    X(CDUP)     \
    X(MKD)      \
    X(RMD)      \
    X(STAT)     \
    X(SIZE)     \
    X(DELE)     \
    X(RNFR)     \
    X(RNTO)     \
    X(PORT)     \
    X(PASV)     \
    X(LIST)     \
    X(NLST)     \
    X(APPE)     \
    X(RETR)     \
    X(STOR)     \
    X(QUIT)     \

enum ftp_command {
#define X(name) FTP_##name,
    FTP_COMMAND_MAP(X)
#undef  X
};

// ftp_status
// XXX(code, name, string)
#define FTP_STATUS_MAP(XXX) \
    XXX(220,    READY,          Ready)  \
    XXX(221,    BYE,            Bye)    \
    XXX(226,    TRANSFER_COMPLETE,  Transfer complete)  \
    XXX(227,    PASV,           Entering Passive Mode)  \
    XXX(331,    PASS,           Password required)      \
    XXX(230,    LOGIN_OK,       Login OK)   \
    XXX(250,    OK,             OK)         \
    XXX(500,    BAD_SYNTAX,     Bad syntax)         \
    XXX(530,    NOT_LOGIN,      Not login)  \

enum ftp_status {
#define XXX(code, name, string) FTP_STATUS_##name = code,
    FTP_STATUS_MAP(XXX)
#undef  XXX
};

// more friendly macros
#define FTP_MKDIR       FTP_MKD
#define FTP_RMDIR       FTP_RMD
#define FTP_APPEND      FTP_APPE
#define FTP_REMOVE      FTP_DELE
#define FTP_DOWNLOAD    FTP_RETR
#define FTP_UPLOAD      FTP_STOR

#define FTP_RECV_BUFSIZE    8192

typedef struct ftp_handle_s {
    int     sockfd;
    char    recvbuf[FTP_RECV_BUFSIZE];
    void*   userdata;
} ftp_handle_t;

BEGIN_EXTERN_C

HV_EXPORT const char* ftp_command_str(enum ftp_command cmd);
HV_EXPORT const char* ftp_status_str(enum ftp_status status);

HV_EXPORT int ftp_connect(ftp_handle_t* hftp, const char* host, int port);
HV_EXPORT int ftp_login(ftp_handle_t* hftp, const char* username, const char* password);
HV_EXPORT int ftp_quit(ftp_handle_t* hftp);

HV_EXPORT int ftp_exec(ftp_handle_t* hftp, const char* cmd, const char* param);

// local => remote
HV_EXPORT int ftp_upload(ftp_handle_t* hftp, const char* local_filepath, const char* remote_filepath);
// remote => local
HV_EXPORT int ftp_download(ftp_handle_t* hftp, const char* remote_filepath, const char* local_filepath);

typedef int (*ftp_download_cb)(ftp_handle_t* hftp, char* buf, int len);
HV_EXPORT int ftp_download_with_cb(ftp_handle_t* hftp, const char* filepath, ftp_download_cb cb);

END_EXTERN_C

#endif // HV_FTP_H_
