#include "dns.h"

#include <string.h>

#include "hdef.h"
#include "hsocket.h"
#include "herr.h"

void dns_free(dns_t* dns) {
    SAFE_FREE(dns->questions);
    SAFE_FREE(dns->answers);
    SAFE_FREE(dns->authorities);
    SAFE_FREE(dns->addtionals);
}

// www.example.com => 3www7example3com
// Returns encoded length including the terminating 0-label, or -1 on error.
// buf must hold at least DNS_NAME_MAXLEN bytes (DNS wire name limit).
int dns_name_encode(const char* domain, char* buf) {
    const char* p = domain;
    char* start = buf;
    char* end = buf + DNS_NAME_MAXLEN;
    char* plen;
    int len = 0;

    if (!domain || !buf) return -1;
    if (buf + 1 >= end) return -1;

    plen = buf++;
    while (*p != '\0') {
        if (*p != '.') {
            ++len;
            if (len > 63) return -1; /* label too long */
            if (buf >= end) return -1;
            *buf++ = *p;
        } else {
            if (len == 0) return -1; /* empty label / consecutive dots */
            *plen = (char)len;
            if (buf >= end) return -1;
            plen = buf++;
            len = 0;
        }
        ++p;
    }
    *plen = (char)len;
    if (buf >= end) return -1;
    *buf++ = '\0';
    return (int)(buf - start);
}

// 3www7example3com => www.example.com
// Returns number of wire bytes consumed (including the terminating 0 label),
// or -1 on error. domain must hold at least DNS_NAME_MAXLEN bytes.
// This function does not follow compression pointers; callers that see a
// pointer prefix must handle it separately.
int dns_name_decode(const char* buf, char* domain) {
    const unsigned char* p;
    const unsigned char* wire_start;
    char* d;
    char* dend;
    int lablen;
    int first = 1;
    size_t wire_max = DNS_NAME_MAXLEN; /* max wire name size including root */

    if (!buf || !domain) return -1;

    p = (const unsigned char*)buf;
    wire_start = p;
    d = domain;
    dend = domain + DNS_NAME_MAXLEN - 1; /* leave room for NUL */

    for (;;) {
        if ((size_t)(p - wire_start) >= wire_max) return -1;
        lablen = *p++;
        if (lablen == 0) {
            *d = '\0';
            return (int)(p - wire_start);
        }
        /* compression pointer not supported in this helper */
        if (lablen >= 192) return -1;
        if (lablen > 63) return -1;
        if ((size_t)(p - wire_start) + (size_t)lablen >= wire_max) return -1;

        if (!first) {
            if (d >= dend) return -1;
            *d++ = '.';
        }
        first = 0;

        if (d + lablen > dend) return -1;
        memcpy(d, p, (size_t)lablen);
        d += lablen;
        p += lablen;
    }
}

int dns_rr_pack(dns_rr_t* rr, char* buf, int len) {
    char* p = buf;
    char encoded_name[DNS_NAME_MAXLEN];
    int encoded_namelen = dns_name_encode(rr->name, encoded_name);
    int packetlen;
    if (encoded_namelen < 0) return -1;
    packetlen = encoded_namelen + 2 + 2 + (rr->data ? (4+2+rr->datalen) : 0);
    if (len < packetlen) {
        return -1;
    }

    memcpy(p, encoded_name, encoded_namelen);
    p += encoded_namelen;
    uint16_t* pushort = (uint16_t*)p;
    *pushort = htons(rr->rtype);
    p += 2;
    pushort = (uint16_t*)p;
    *pushort = htons(rr->rclass);
    p += 2;

    // ...
    if (rr->datalen && rr->data) {
        uint32_t* puint = (uint32_t*)p;
        *puint = htonl(rr->ttl);
        p += 4;
        pushort = (uint16_t*)p;
        *pushort = htons(rr->datalen);
        p += 2;
        memcpy(p, rr->data, rr->datalen);
        p += rr->datalen;
    }
    return packetlen;
}

int dns_rr_unpack(char* buf, int len, dns_rr_t* rr, int is_question) {
    char* p = buf;
    int off = 0;
    int namelen = 0;
    if (len < 1) return -1;
    if (*(uint8_t*)p >= 192) {
        /* Compression pointer: skip 2 bytes and leave name empty/NUL. */
        if (len < 2) return -1;
        namelen = 2;
        rr->name[0] = '\0';
        //uint16_t nameoff = (*(uint8_t*)p - 192) * 256 + *(uint8_t*)(p+1);
    }
    else {
        namelen = dns_name_decode(buf, rr->name);
    }
    if (namelen < 0) return -1;
    if (namelen > len) return -1;
    p += namelen;
    off += namelen;

    if (len < off + 4) return -1;
    uint16_t* pushort = (uint16_t*)p;
    rr->rtype = ntohs(*pushort);
    p += 2;
    pushort = (uint16_t*)p;
    rr->rclass = ntohs(*pushort);
    p += 2;
    off += 4;

    if (!is_question) {
        if (len < off + 6) return -1;
        uint32_t* puint = (uint32_t*)p;
        rr->ttl = ntohl(*puint);
        p += 4;
        pushort = (uint16_t*)p;
        rr->datalen = ntohs(*pushort);
        p += 2;
        off += 6;
        if (len < off + rr->datalen) return -1;
        rr->data = p;
        p += rr->datalen;
        off += rr->datalen;
    }
    return off;
}

int dns_pack(dns_t* dns, char* buf, int len) {
    if (len < sizeof(dnshdr_t)) return -1;
    int off = 0;
    dnshdr_t* hdr = &dns->hdr;
    dnshdr_t htonhdr = dns->hdr;
    htonhdr.transaction_id = htons(hdr->transaction_id);
    htonhdr.nquestion = htons(hdr->nquestion);
    htonhdr.nanswer = htons(hdr->nanswer);
    htonhdr.nauthority = htons(hdr->nauthority);
    htonhdr.naddtional = htons(hdr->naddtional);
    memcpy(buf, &htonhdr, sizeof(dnshdr_t));
    off += sizeof(dnshdr_t);
    int i;
    for (i = 0; i < hdr->nquestion; ++i) {
        int packetlen = dns_rr_pack(dns->questions+i, buf+off, len-off);
        if (packetlen < 0) return -1;
        off += packetlen;
    }
    for (i = 0; i < hdr->nanswer; ++i) {
        int packetlen = dns_rr_pack(dns->answers+i, buf+off, len-off);
        if (packetlen < 0) return -1;
        off += packetlen;
    }
    for (i = 0; i < hdr->nauthority; ++i) {
        int packetlen = dns_rr_pack(dns->authorities+i, buf+off, len-off);
        if (packetlen < 0) return -1;
        off += packetlen;
    }
    for (i = 0; i < hdr->naddtional; ++i) {
        int packetlen = dns_rr_pack(dns->addtionals+i, buf+off, len-off);
        if (packetlen < 0) return -1;
        off += packetlen;
    }
    return off;
}

int dns_unpack(char* buf, int len, dns_t* dns) {
    memset(dns, 0, sizeof(dns_t));
    if (len < sizeof(dnshdr_t)) return -1;
    int off = 0;
    dnshdr_t* hdr = &dns->hdr;
    memcpy(hdr, buf, sizeof(dnshdr_t));
    off += sizeof(dnshdr_t);
    hdr->transaction_id = ntohs(hdr->transaction_id);
    hdr->nquestion = ntohs(hdr->nquestion);
    hdr->nanswer = ntohs(hdr->nanswer);
    hdr->nauthority = ntohs(hdr->nauthority);
    hdr->naddtional = ntohs(hdr->naddtional);
    int i;
    if (hdr->nquestion) {
        int bytes = hdr->nquestion * sizeof(dns_rr_t);
        SAFE_ALLOC(dns->questions, bytes);
        for (i = 0; i < hdr->nquestion; ++i) {
            int packetlen = dns_rr_unpack(buf+off, len-off, dns->questions+i, 1);
            if (packetlen < 0) return -1;
            off += packetlen;
        }
    }
    if (hdr->nanswer) {
        int bytes = hdr->nanswer * sizeof(dns_rr_t);
        SAFE_ALLOC(dns->answers, bytes);
        for (i = 0; i < hdr->nanswer; ++i) {
            int packetlen = dns_rr_unpack(buf+off, len-off, dns->answers+i, 0);
            if (packetlen < 0) return -1;
            off += packetlen;
        }
    }
    if (hdr->nauthority) {
        int bytes = hdr->nauthority * sizeof(dns_rr_t);
        SAFE_ALLOC(dns->authorities, bytes);
        for (i = 0; i < hdr->nauthority; ++i) {
            int packetlen = dns_rr_unpack(buf+off, len-off, dns->authorities+i, 0);
            if (packetlen < 0) return -1;
            off += packetlen;
        }
    }
    if (hdr->naddtional) {
        int bytes = hdr->naddtional * sizeof(dns_rr_t);
        SAFE_ALLOC(dns->addtionals, bytes);
        for (i = 0; i < hdr->naddtional; ++i) {
            int packetlen = dns_rr_unpack(buf+off, len-off, dns->addtionals+i, 0);
            if (packetlen < 0) return -1;
            off += packetlen;
        }
    }
    return off;
}

// dns_pack -> sendto -> recvfrom -> dns_unpack
int dns_query(dns_t* query, dns_t* response, const char* nameserver) {
    char buf[1024];
    int buflen = sizeof(buf);
    buflen = dns_pack(query, buf, buflen);
    if (buflen < 0) {
        return buflen;
    }
#ifdef OS_WIN
    WSAInit();
#endif
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return ERR_SOCKET;
    }
    so_sndtimeo(sockfd, 5000);
    so_rcvtimeo(sockfd, 5000);
    int ret = 0;
    int nsend, nrecv;
    int nparse;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    memset(&addr, 0, addrlen);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(nameserver);
    addr.sin_port = htons(DNS_PORT);
    nsend = sendto(sockfd, buf, buflen, 0, (struct sockaddr*)&addr, addrlen);
    if (nsend != buflen) {
        ret = ERR_SENDTO;
        goto error;
    }
    nrecv = recvfrom(sockfd, buf, sizeof(buf), 0, (struct sockaddr*)&addr, &addrlen);
    if (nrecv <= 0) {
        ret = ERR_RECVFROM;
        goto error;
    }

    nparse = dns_unpack(buf, nrecv, response);
    if (nparse != nrecv) {
        ret = -ERR_INVALID_PACKAGE;
        goto error;
    }

error:
    if (sockfd != INVALID_SOCKET) {
        closesocket(sockfd);
    }
    return ret;
}

int nslookup(const char* domain, uint32_t* addrs, int naddr, const char* nameserver) {
    dns_t query;
    memset(&query, 0, sizeof(query));
    query.hdr.transaction_id = getpid();
    query.hdr.qr = DNS_QUERY;
    query.hdr.rd = 1;
    query.hdr.nquestion = 1;

    dns_rr_t question;
    memset(&question, 0, sizeof(question));
    strncpy(question.name, domain, sizeof(question.name));
    question.rtype = DNS_TYPE_A;
    question.rclass = DNS_CLASS_IN;

    query.questions = &question;

    dns_t resp;
    memset(&resp, 0, sizeof(resp));
    int ret = dns_query(&query, &resp, nameserver);
    if (ret != 0) {
        return ret;
    }

    dns_rr_t* rr = resp.answers;
    int addr_cnt = 0;
    if (resp.hdr.transaction_id != query.hdr.transaction_id ||
        resp.hdr.qr != DNS_RESPONSE ||
        resp.hdr.rcode != 0) {
        ret = -ERR_MISMATCH;
        goto end;
    }

    if (resp.hdr.nanswer == 0) {
        ret = 0;
        goto end;
    }

    for (int i = 0; i < resp.hdr.nanswer; ++i, ++rr) {
        if (rr->rtype == DNS_TYPE_A) {
            if (addr_cnt < naddr && rr->datalen == 4) {
                memcpy(addrs+addr_cnt, rr->data, 4);
            }
            ++addr_cnt;
        }
        /*
        else if (rr->rtype == DNS_TYPE_CNAME) {
            char name[256];
            dns_name_decode(rr->data, name);
        }
        */
    }
    ret = addr_cnt;
end:
    dns_free(&resp);
    return ret;
}
