#include "smtp.h"

#include "hsocket.h"
#include "herr.h"
#include "base64.h"

const char* smtp_command_str(enum smtp_command cmd) {
    switch (cmd) {
#define XX(name, string) case SMTP_##name: return #string;
    SMTP_COMMAND_MAP(XX)
#undef  XX
    default: return "<unknown>";
    }
}

const char* smtp_status_str(enum smtp_status status) {
    switch (status) {
#define XXX(code, name, string) case SMTP_STATUS_##name: return #string;
    SMTP_STATUS_MAP(XXX)
#undef  XXX
    default: return "<unknown>";
    }
}

int smtp_build_command(enum smtp_command cmd, const char* param, char* buf, int buflen) {
    switch (cmd) {
    // unary
    case SMTP_DATA:
    case SMTP_QUIT:
        return snprintf(buf, buflen, "%s\r\n", smtp_command_str(cmd));
    // <address>
    case SMTP_MAIL:
    case SMTP_RCPT:
        return snprintf(buf, buflen, "%s <%s>\r\n", smtp_command_str(cmd), param);
    default:
        return snprintf(buf, buflen, "%s %s\r\n", smtp_command_str(cmd), param);
    }
}

// EHLO => AUTH PLAIN => MAIL => RCPT => DATA => data => EOB => QUIT
int sendmail(const char* smtp_server,
             const char* username,
             const char* password,
             mail_t* mail) {
    char buf[1024] = {0};
    int  buflen = sizeof(buf);
    int  cmdlen = 0;
    int  status_code = 0;
    char basic[256];
    int  basiclen;

    int sockfd = ConnectTimeout(smtp_server, SMTP_PORT, DEFAULT_CONNECT_TIMEOUT);
    if (sockfd < 0) {
        return sockfd;
    }
    so_sndtimeo(sockfd, 5000);
    so_rcvtimeo(sockfd, 5000);

    int ret, nsend, nrecv;
    nrecv = recv(sockfd, buf, buflen, 0);
    if (nrecv <= 0) {
        ret = ERR_RECV;
        goto error;
    }
    status_code = atoi(buf);
    if (status_code != SMTP_STATUS_READY) {
        ret = status_code;
        goto error;
    }
    // EHLO smtp.xxx.com\r\n
    cmdlen = smtp_build_command(SMTP_EHLO, smtp_server, buf, buflen);
    nsend = send(sockfd, buf, cmdlen, 0);
    if (nsend != cmdlen) {
        ret = ERR_SEND;
        goto error;
    }
    nrecv = recv(sockfd, buf, buflen, 0);
    if (nrecv <= 0) {
        ret = ERR_RECV;
        goto error;
    }
    status_code = atoi(buf);
    if (status_code != SMTP_STATUS_OK) {
        ret = status_code;
        goto error;
    }
    // AUTH PLAIN\r\n
    cmdlen = smtp_build_command(SMTP_AUTH, "PLAIN", buf, buflen);
    nsend = send(sockfd, buf, cmdlen, 0);
    if (nsend != cmdlen) {
        ret = ERR_SEND;
        goto error;
    }
    nrecv = recv(sockfd, buf, buflen, 0);
    if (nrecv <= 0) {
        ret = ERR_RECV;
        goto error;
    }
    status_code = atoi(buf);
    if (status_code != SMTP_STATUS_AUTH) {
        ret = status_code;
        goto error;
    }
    {
        // BASE64 \0username\0password
        int usernamelen = strlen(username);
        int passwordlen = strlen(password);
        basic[0] = '\0';
        memcpy(basic+1, username, usernamelen);
        basic[1+usernamelen] = '\0';
        memcpy(basic+1+usernamelen+1, password, passwordlen);
        basiclen = 1 + usernamelen + 1 + passwordlen;
    }
    base64_encode((unsigned char*)basic, basiclen, buf);
    cmdlen = BASE64_ENCODE_OUT_SIZE(basiclen);
    buf[cmdlen] = '\r';
    buf[cmdlen+1] = '\n';
    cmdlen += 2;
    nsend = send(sockfd, buf, cmdlen, 0);
    if (nsend != cmdlen) {
        ret = ERR_SEND;
        goto error;
    }
    nrecv = recv(sockfd, buf, buflen, 0);
    if (nrecv <= 0) {
        ret = ERR_RECV;
        goto error;
    }
    status_code = atoi(buf);
    if (status_code != SMTP_STATUS_AUTH_SUCCESS) {
        ret = status_code;
        goto error;
    }
    // MAIL FROM: <from>\r\n
    cmdlen = smtp_build_command(SMTP_MAIL, mail->from, buf, buflen);
    nsend = send(sockfd, buf, cmdlen, 0);
    if (nsend != cmdlen) {
        ret = ERR_SEND;
        goto error;
    }
    nrecv = recv(sockfd, buf, buflen, 0);
    if (nrecv <= 0) {
        ret = ERR_RECV;
        goto error;
    }
    status_code = atoi(buf);
    if (status_code != SMTP_STATUS_OK) {
        ret = status_code;
        goto error;
    }
    // RCPT TO: <to>\r\n
    cmdlen = smtp_build_command(SMTP_RCPT, mail->to, buf, buflen);
    nsend = send(sockfd, buf, cmdlen, 0);
    if (nsend != cmdlen) {
        ret = ERR_SEND;
        goto error;
    }
    nrecv = recv(sockfd, buf, buflen, 0);
    if (nrecv <= 0) {
        ret = ERR_RECV;
        goto error;
    }
    status_code = atoi(buf);
    if (status_code != SMTP_STATUS_OK) {
        ret = status_code;
        goto error;
    }
    // DATA\r\n
    cmdlen = smtp_build_command(SMTP_DATA, NULL, buf, buflen);
    nsend = send(sockfd, buf, cmdlen, 0);
    if (nsend != cmdlen) {
        ret = ERR_SEND;
        goto error;
    }
    nrecv = recv(sockfd, buf, buflen, 0);
    if (nrecv <= 0) {
        ret = ERR_RECV;
        goto error;
    }
    status_code = atoi(buf);
    // SMTP_STATUS_DATA
    if (status_code >= 400) {
        ret = status_code;
        goto error;
    }
    // From:
    cmdlen = snprintf(buf, buflen, "From:%s\r\n", mail->from);
    nsend = send(sockfd, buf, cmdlen, 0);
    if (nsend != cmdlen) {
        ret = ERR_SEND;
        goto error;
    }
    // To:
    cmdlen = snprintf(buf, buflen, "To:%s\r\n", mail->to);
    nsend = send(sockfd, buf, cmdlen, 0);
    if (nsend != cmdlen) {
        ret = ERR_SEND;
        goto error;
    }
    // Subject:
    cmdlen = snprintf(buf, buflen, "Subject:%s\r\n\r\n", mail->subject);
    nsend = send(sockfd, buf, cmdlen, 0);
    if (nsend != cmdlen) {
        ret = ERR_SEND;
        goto error;
    }
    // body
    cmdlen = strlen(mail->body);
    nsend = send(sockfd, mail->body, cmdlen, 0);
    if (nsend != cmdlen) {
        ret = ERR_SEND;
        goto error;
    }
    // EOB
    nsend = send(sockfd, SMTP_EOB, SMTP_EOB_LEN, 0);
    if (nsend != SMTP_EOB_LEN) {
        ret = ERR_SEND;
        goto error;
    }
    nrecv = recv(sockfd, buf, buflen, 0);
    if (nrecv <= 0) {
        ret = ERR_SEND;
        goto error;
    }
    status_code = atoi(buf);
    if (status_code != SMTP_STATUS_OK) {
        ret = status_code;
        goto error;
    }
    // QUIT\r\n
    cmdlen = smtp_build_command(SMTP_QUIT, NULL, buf, buflen);
    nsend = send(sockfd, buf, cmdlen, 0);
    if (nsend != cmdlen) {
        ret = ERR_SEND;
        goto error;
    }
    nrecv = recv(sockfd, buf, buflen, 0);
    if (nrecv <= 0) {
        ret = ERR_RECV;
        goto error;
    }
    /*
    status_code = atoi(buf);
    if (status_code != SMTP_STATUS_BYE) {
        ret = status_code;
        goto error;
    }
    */
    ret = SMTP_STATUS_OK;

error:
    if (sockfd != INVALID_SOCKET) {
        closesocket(sockfd);
    }
    return ret;
}
