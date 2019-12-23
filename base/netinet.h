#ifndef HV_NETINET_H_
#define HV_NETINET_H_

#include "hplatform.h"

/*
#ifdef OS_UNIX
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netinet/tcp.h>
#include <netinet/ip_icmp.h>
typedef struct iphdr    iphdr_t;
typedef struct udphdr   udphdr_t;
typedef struct tcphdr   tcphdr_t;

typedef struct icmphdr  icmphdr_t;
typedef struct icmp     icmp_t;
#else
*/
// sizeof(iphdr_t) = 20
typedef struct iphdr_s {
#if BYTE_ORDER == LITTLE_ENDIAN
    uint8_t     ihl:4; // ip header length
    uint8_t     version:4;
#elif BYTE_ORDER == BIG_ENDIAN
    uint8_t     version:4;
    uint8_t     ihl:4;
#else
#error "BYTE_ORDER undefined!"
#endif
    uint8_t     tos; // type of service
    uint16_t    tot_len; // total length
    uint16_t    id;
    uint16_t    frag_off; // fragment offset
    uint8_t     ttl; // Time To Live
    uint8_t     protocol;
    uint16_t    check; // checksum
    uint32_t    saddr; // srcaddr
    uint32_t    daddr; // dstaddr
    /*The options start here.*/
} iphdr_t;

// sizeof(udphdr_t) = 8
typedef struct udphdr_s {
    uint16_t    source; // source port
    uint16_t    dest;   // dest   port
    uint16_t    len;    // udp length
    uint16_t    check;  // checksum
} udphdr_t;

// sizeof(tcphdr_t) = 20
typedef struct tcphdr_s {
    uint16_t    source; // source port
    uint16_t    dest;   // dest   port
    uint32_t    seq;    // sequence
    uint32_t    ack_seq;
#if BYTE_ORDER == LITTLE_ENDIAN
    uint16_t    res1:4;
    uint16_t    doff:4;
    uint16_t    fin:1;
    uint16_t    syn:1;
    uint16_t    rst:1;
    uint16_t    psh:1;
    uint16_t    ack:1;
    uint16_t    urg:1;
    uint16_t    res2:2;
#elif BYTE_ORDER == BIG_ENDIAN
    uint16_t    doff:4;
    uint16_t    res1:4;
    uint16_t    res2:2;
    uint16_t    urg:1;
    uint16_t    ack:1;
    uint16_t    psh:1;
    uint16_t    rst:1;
    uint16_t    syn:1;
    uint16_t    fin:1;
#else
#error "BYTE_ORDER undefined!"
#endif
    uint16_t    window;
    uint16_t    check; // checksum
    uint16_t    urg_ptr; // urgent pointer
} tcphdr_t;

//----------------------icmp----------------------------------
#define ICMP_ECHOREPLY		0	/* Echo Reply			*/
#define ICMP_DEST_UNREACH	3	/* Destination Unreachable	*/
#define ICMP_SOURCE_QUENCH	4	/* Source Quench		*/
#define ICMP_REDIRECT		5	/* Redirect (change route)	*/
#define ICMP_ECHO		8	/* Echo Request			*/
#define ICMP_TIME_EXCEEDED	11	/* Time Exceeded		*/
#define ICMP_PARAMETERPROB	12	/* Parameter Problem		*/
#define ICMP_TIMESTAMP		13	/* Timestamp Request		*/
#define ICMP_TIMESTAMPREPLY	14	/* Timestamp Reply		*/
#define ICMP_INFO_REQUEST	15	/* Information Request		*/
#define ICMP_INFO_REPLY		16	/* Information Reply		*/
#define ICMP_ADDRESS		17	/* Address Mask Request		*/
#define ICMP_ADDRESSREPLY	18	/* Address Mask Reply		*/

// sizeof(icmphdr_t) = 8
typedef struct icmphdr_s {
    uint8_t     type;   // message type
    uint8_t     code;   // type sub-code
    uint16_t    checksum;
    union {
        struct {
            uint16_t    id;
            uint16_t    sequence;
        } echo;
        uint32_t    gateway;
        struct {
            uint16_t    reserved;
            uint16_t    mtu;
        } frag;
    } un;
} icmphdr_t;

typedef struct icmp_s {
    uint8_t     icmp_type;
    uint8_t     icmp_code;
    uint16_t    icmp_cksum;
    union {
        uint8_t ih_pptr;
        struct in_addr ih_gwaddr;
        struct ih_idseq {
            uint16_t icd_id;
            uint16_t icd_seq;
        } ih_idseq;
        uint32_t    ih_void;

        struct ih_pmtu {
            uint16_t ipm_void;
            uint16_t ipm_nextmtu;
        } ih_pmtu;

        struct ih_rtradv {
            uint8_t irt_num_addrs;
            uint8_t irt_wpa;
            uint16_t irt_lifetime;
        } ih_rtradv;
    } icmp_hun;
#define	icmp_pptr	icmp_hun.ih_pptr
#define	icmp_gwaddr	icmp_hun.ih_gwaddr
#define	icmp_id		icmp_hun.ih_idseq.icd_id
#define	icmp_seq	icmp_hun.ih_idseq.icd_seq
#define	icmp_void	icmp_hun.ih_void
#define	icmp_pmvoid	icmp_hun.ih_pmtu.ipm_void
#define	icmp_nextmtu	icmp_hun.ih_pmtu.ipm_nextmtu
#define	icmp_num_addrs	icmp_hun.ih_rtradv.irt_num_addrs
#define	icmp_wpa	icmp_hun.ih_rtradv.irt_wpa
#define	icmp_lifetime	icmp_hun.ih_rtradv.irt_lifetime

    union {
        struct {
            uint32_t its_otime;
            uint32_t its_rtime;
            uint32_t its_ttime;
        } id_ts;
        /*
        struct {
            struct ip idi_ip;
        } id_ip;
        struct icmp_ra_addr id_radv;
        */
        uint32_t id_mask;
        uint8_t  id_data[1];
    } icmp_dun;
#define	icmp_otime	icmp_dun.id_ts.its_otime
#define	icmp_rtime	icmp_dun.id_ts.its_rtime
#define	icmp_ttime	icmp_dun.id_ts.its_ttime
#define	icmp_ip		icmp_dun.id_ip.idi_ip
#define	icmp_radv	icmp_dun.id_radv
#define	icmp_mask	icmp_dun.id_mask
#define	icmp_data	icmp_dun.id_data
} icmp_t;
//#endif

static inline uint16_t checksum(uint8_t* buf, int len) {
    unsigned int sum = 0;
    uint16_t* ptr = (uint16_t*)buf;
    while(len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    if(len) {
        sum += *(uint8_t*)ptr;
    }
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    return (uint16_t)(~sum);
};

#endif // HV_NETINET_H_
