#ifndef HV_DNS_H_
#define HV_DNS_H_

#include "hexport.h"
#include "hplatform.h"

#define DNS_PORT        53

#define DNS_QUERY       0
#define DNS_RESPONSE    1

#define DNS_TYPE_A      1   // ipv4
#define DNS_TYPE_NS     2
#define DNS_TYPE_CNAME  5
#define DNS_TYPE_SOA    6
#define DNS_TYPE_WKS    11
#define DNS_TYPE_PTR    12
#define DNS_TYPE_HINFO  13
#define DNS_TYPE_MX     15
#define DNS_TYPE_AAAA   28  // ipv6
#define DNS_TYPE_AXFR   252
#define DNS_TYPE_ANY    255

#define DNS_CLASS_IN    1

#define DNS_NAME_MAXLEN 256

// sizeof(dnshdr_t) = 12
typedef struct dnshdr_s {
    uint16_t    transaction_id;
    // flags
#if BYTE_ORDER == LITTLE_ENDIAN
    uint8_t     rd:1;
    uint8_t     tc:1;
    uint8_t     aa:1;
    uint8_t     opcode:4;
    uint8_t     qr:1;

    uint8_t     rcode:4;
    uint8_t     cd:1;
    uint8_t     ad:1;
    uint8_t     res:1;
    uint8_t     ra:1;
#elif BYTE_ORDER == BIG_ENDIAN
    uint8_t    qr:1;   // DNS_QUERY or DNS_RESPONSE
    uint8_t    opcode:4;
    uint8_t    aa:1;   // authoritative
    uint8_t    tc:1;   // truncated
    uint8_t    rd:1;   // recursion desired

    uint8_t    ra:1;   // recursion available
    uint8_t    res:1;  // reserved
    uint8_t    ad:1;   // authenticated data
    uint8_t    cd:1;   // checking disable
    uint8_t    rcode:4;
#else
#error "BYTE_ORDER undefined!"
#endif
    uint16_t    nquestion;
    uint16_t    nanswer;
    uint16_t    nauthority;
    uint16_t    naddtional;
} dnshdr_t;

typedef struct dns_rr_s {
    char        name[DNS_NAME_MAXLEN]; // original domain, such as www.example.com
    uint16_t    rtype;
    uint16_t    rclass;
    uint32_t    ttl;
    uint16_t    datalen;
    char*       data;
} dns_rr_t;

typedef struct dns_s {
    dnshdr_t        hdr;
    dns_rr_t*       questions;
    dns_rr_t*       answers;
    dns_rr_t*       authorities;
    dns_rr_t*       addtionals;
} dns_t;

BEGIN_EXTERN_C

// www.example.com => 3www7example3com
HV_EXPORT int dns_name_encode(const char* domain, char* buf);
// 3www7example3com => www.example.com
HV_EXPORT int dns_name_decode(const char* buf, char* domain);

HV_EXPORT int dns_rr_pack(dns_rr_t* rr, char* buf, int len);
HV_EXPORT int dns_rr_unpack(char* buf, int len, dns_rr_t* rr, int is_question);

HV_EXPORT int dns_pack(dns_t* dns, char* buf, int len);
HV_EXPORT int dns_unpack(char* buf, int len, dns_t* dns);
// NOTE: free dns->rrs
HV_EXPORT void dns_free(dns_t* dns);

// dns_pack -> sendto -> recvfrom -> dns_unpack
HV_EXPORT int dns_query(dns_t* query, dns_t* response, const char* nameserver DEFAULT("127.0.1.1"));

// domain -> dns_t query; -> dns_query -> dns_t response; -> addrs
HV_EXPORT int nslookup(const char* domain, uint32_t* addrs, int naddr, const char* nameserver DEFAULT("127.0.1.1"));

END_EXTERN_C

#endif // HV_DNS_H_
