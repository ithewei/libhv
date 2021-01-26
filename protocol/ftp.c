#include "ftp.h"
#include "hsocket.h"
#include "herr.h"

const char* ftp_command_str(enum ftp_command cmd) {
    switch (cmd) {
#define X(name) case FTP_##name: return #name;
    FTP_COMMAND_MAP(X)
#undef  X
    default: return "<unknown>";
    }
}

const char* ftp_status_str(enum ftp_status status) {
    switch (status) {
#define XXX(code, name, string) case FTP_STATUS_##name: return #string;
    FTP_STATUS_MAP(XXX)
#undef  XXX
    default: return "<unknown>";
    }
}

int ftp_connect(ftp_handle_t* hftp, const char* host, int port) {
    int sockfd = ConnectTimeout(host, port, DEFAULT_CONNECT_TIMEOUT);
    if (sockfd < 0) {
        return sockfd;
    }
    so_sndtimeo(sockfd, 5000);
    so_rcvtimeo(sockfd, 5000);
    hftp->sockfd = sockfd;
    int ret = 0;
    int status_code = 0;
    memset(hftp->recvbuf, 0, FTP_RECV_BUFSIZE);
    int nrecv = recv(sockfd, hftp->recvbuf, FTP_RECV_BUFSIZE, 0);
    if (nrecv <= 0) {
        ret = ERR_RECV;
        goto error;
    }
    status_code = atoi(hftp->recvbuf);
    if (status_code != FTP_STATUS_READY) {
        ret = status_code;
        goto error;
    }
    return 0;

error:
    closesocket(sockfd);
    return ret;
}

int ftp_login(ftp_handle_t* hftp, const char* username, const char* password) {
    int status_code = ftp_exec(hftp, "USER", username);
    status_code = ftp_exec(hftp, "PASS", password);
    return status_code == FTP_STATUS_LOGIN_OK ? 0 : status_code;
}

int ftp_quit(ftp_handle_t* hftp) {
    ftp_exec(hftp, "QUIT", NULL);
    closesocket(hftp->sockfd);
    return 0;
}

int ftp_exec(ftp_handle_t* hftp, const char* cmd, const char* param) {
    char buf[1024];
    int len = 0;
    if (param && *param) {
        len = snprintf(buf, sizeof(buf), "%s %s\r\n", cmd, param);
    }
    else {
        len = snprintf(buf, sizeof(buf), "%s\r\n", cmd);
    }
    int nsend, nrecv;
    int ret = 0;
    nsend = send(hftp->sockfd, buf, len, 0);
    if (nsend != len) {
        ret = ERR_SEND;
        goto error;
    }
    //printf("> %s", buf);
    memset(hftp->recvbuf, 0, FTP_RECV_BUFSIZE);
    nrecv = recv(hftp->sockfd, hftp->recvbuf, FTP_RECV_BUFSIZE, 0);
    if (nrecv <= 0) {
        ret = ERR_RECV;
        goto error;
    }
    //printf("< %s", hftp->recvbuf);
    return atoi(hftp->recvbuf);
error:
    closesocket(hftp->sockfd);
    return ret;
}

static int ftp_parse_pasv(const char* resp, char* host, int* port) {
    // 227 Entering Passive Mode (127,0,0,1,4,51)
    const char* str = strchr(resp, '(');
    if (str == NULL) {
        return ERR_RESPONSE;
    }
    int arr[6];
    sscanf(str, "(%d,%d,%d,%d,%d,%d)",
            &arr[0], &arr[1], &arr[2], &arr[3], &arr[4], &arr[5]);
    sprintf(host, "%d.%d.%d.%d", arr[0], arr[1], arr[2], arr[3]);
    *port = arr[4] << 8 | arr[5];
    return 0;
}

int ftp_download_with_cb(ftp_handle_t* hftp, const char* filepath, ftp_download_cb cb) {
    int status_code = ftp_exec(hftp, "PASV", NULL);
    if (status_code != FTP_STATUS_PASV) {
        return status_code;
    }
    char host[64];
    int port = 0;
    int ret = ftp_parse_pasv(hftp->recvbuf, host, &port);
    if (ret != 0) {
        return ret;
    }
    //ftp_exec(hftp, "RETR", filepath);
    char request[1024];
    int len = snprintf(request, sizeof(request), "RETR %s\r\n", filepath);
    int nsend = send(hftp->sockfd, request, len, 0);
    if (nsend != len) {
        closesocket(hftp->sockfd);
        return ERR_SEND;
    }
    //printf("> %s", request);
    int sockfd = ConnectTimeout(host, port, DEFAULT_CONNECT_TIMEOUT);
    if (sockfd < 0) {
        return sockfd;
    }
    int nrecv = recv(hftp->sockfd, hftp->recvbuf, FTP_RECV_BUFSIZE, 0);
    if (nrecv <= 0) {
        closesocket(hftp->sockfd);
        return ERR_RECV;
    }
    //printf("< %s", hftp->recvbuf);
    {
        // you can create thread to recv data
        char recvbuf[1024];
        int ntotal = 0;
        while (1) {
            nrecv = recv(sockfd, recvbuf, sizeof(recvbuf), 0);
            if (cb) {
                cb(hftp, recvbuf, nrecv);
            }
            if (nrecv <= 0) break;
            ntotal += nrecv;
        }
    }
    closesocket(sockfd);
    nrecv = recv(hftp->sockfd, hftp->recvbuf, FTP_RECV_BUFSIZE, 0);
    if (nrecv <= 0) {
        closesocket(hftp->sockfd);
        return ERR_RECV;
    }
    //printf("< %s", hftp->recvbuf);
    status_code = atoi(hftp->recvbuf);
    return status_code == FTP_STATUS_TRANSFER_COMPLETE ? 0 : status_code;
}

// local => remote
int ftp_upload(ftp_handle_t* hftp, const char* local_filepath, const char* remote_filepath) {
    int status_code = ftp_exec(hftp, "PASV", NULL);
    if (status_code != FTP_STATUS_PASV) {
        return status_code;
    }
    char host[64];
    int port = 0;
    int ret = ftp_parse_pasv(hftp->recvbuf, host, &port);
    if (ret != 0) {
        return ret;
    }
    //ftp_exec(hftp, "STOR", remote_filepath);
    char request[1024];
    int len = snprintf(request, sizeof(request), "STOR %s\r\n", remote_filepath);
    int nsend = send(hftp->sockfd, request, len, 0);
    if (nsend != len) {
        closesocket(hftp->sockfd);
        return ERR_SEND;
    }
    //printf("> %s", request);
    int sockfd = ConnectTimeout(host, port, DEFAULT_CONNECT_TIMEOUT);
    if (sockfd < 0) {
        return sockfd;
    }
    int nrecv = recv(hftp->sockfd, hftp->recvbuf, FTP_RECV_BUFSIZE, 0);
    if (nrecv <= 0) {
        closesocket(hftp->sockfd);
        return ERR_RECV;
    }
    //printf("< %s", hftp->recvbuf);
    {
        // you can create thread to send data
        FILE* fp = fopen(local_filepath, "rb");
        if (fp == NULL) {
            closesocket(sockfd);
            return ERR_OPEN_FILE;
        }
        char sendbuf[1024];
        int nread, nsend;
        int ntotal = 0;
        while (1) {
            nread = fread(sendbuf, 1, sizeof(sendbuf), fp);
            if (nread == 0) break;
            nsend = send(sockfd, sendbuf, nread, 0);
            if (nsend != nread) break;
            ntotal += nsend;
        }
        fclose(fp);
    }
    closesocket(sockfd);
    nrecv = recv(hftp->sockfd, hftp->recvbuf, FTP_RECV_BUFSIZE, 0);
    if (nrecv <= 0) {
        closesocket(hftp->sockfd);
        return ERR_RECV;
    }
    //printf("< %s", hftp->recvbuf);
    status_code = atoi(hftp->recvbuf);
    return status_code == FTP_STATUS_TRANSFER_COMPLETE ? 0 : status_code;
}

static int s_ftp_download_cb(ftp_handle_t* hftp, char* buf, int len) {
    FILE* fp = (FILE*)hftp->userdata;
    if (fp == NULL) return -1;
    if (len <= 0) {
        fclose(fp);
        hftp->userdata = NULL;
        return 0;
    }
    return fwrite(buf, 1, len, fp);
}

// remote => local
int ftp_download(ftp_handle_t* hftp, const char* remote_filepath, const char* local_filepath) {
    FILE* fp = fopen(local_filepath, "wb");
    if (fp == NULL) {
        return ERR_OPEN_FILE;
    }
    hftp->userdata = (void*)fp;
    int ret = ftp_download_with_cb(hftp, remote_filepath, s_ftp_download_cb);
    // ensure fclose
    if (hftp->userdata != NULL) {
        fclose(fp);
        hftp->userdata = NULL;
    }
    return ret;
}
