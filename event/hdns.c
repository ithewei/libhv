/*
 * Asynchronous DNS resolver — implementation.
 *
 * A self-contained, non-blocking, native UDP DNS resolver that runs entirely
 * inside the hloop event loop. See event/hdns.h for the public API.
 *
 * This file is intentionally independent from protocol/dns.c (a synchronous
 * demo): it carries its own complete DNS wire codec, config loading, query
 * engine and TTL cache.
 *
 * Organization:
 *   1. DNS wire codec        (build query, parse response, name compression)
 *   2. Config                (nameservers, hosts) with lazy process-wide cache
 *   3. Per-loop resolver      (owns UDP io, in-flight table, cache)
 *   4. Cache                  (TTL positive + negative)
 *   5. Query lifecycle        (hdns_resolve / hdns_cancel / timers / completion)
 */

#include "hdns.h"
#include "hevent.h"

#include "hdef.h"
#include "hbase.h"
#include "hlog.h"
#include "htime.h"
#include "hmutex.h"
#include "list.h"

#ifdef OS_WIN
#include <iphlpapi.h>
#ifdef _MSC_VER
#pragma comment(lib, "iphlpapi.lib")
#endif
#endif

// portable strtok_r (MSVC provides strtok_s with the same signature)
#ifdef _MSC_VER
#define hdns_strtok_r   strtok_s
#else
#define hdns_strtok_r   strtok_r
#endif

//==============================================================================
// Constants
//==============================================================================
#define HDNS_TYPE_A          1
#define HDNS_TYPE_NS         2
#define HDNS_TYPE_CNAME      5
#define HDNS_TYPE_AAAA       28
#define HDNS_CLASS_IN        1

#define HDNS_HDR_SIZE        12
#define HDNS_UDP_BUFSIZE     1500        // enough for a non-EDNS UDP response
#define HDNS_MAX_NAMESERVERS 4
#define HDNS_MAX_CNAME_HOPS  16          // guard against CNAME loops
#define HDNS_MAX_LABEL_JUMPS 128         // guard against compression-pointer loops

#define HDNS_CACHE_MIN_TTL   1           // seconds
#define HDNS_CACHE_MAX_TTL   86400       // seconds
#define HDNS_CACHE_NEG_TTL   5           // seconds, negative cache
#define HDNS_CACHE_MAX_ENTRIES 256

//==============================================================================
// 1. DNS wire codec
//==============================================================================

// Encode "www.example.com" into "\3www\7example\3com\0" at dst.
// Returns encoded length (including trailing 0), or -1 on error.
static int hdns__encode_name(const char* name, uint8_t* dst, int dstlen) {
    int off = 0;
    const char* p = name;
    while (*p) {
        const char* dot = strchr(p, '.');
        int label_len = dot ? (int)(dot - p) : (int)strlen(p);
        if (label_len <= 0 || label_len > 63) return -1;
        if (off + 1 + label_len >= dstlen) return -1;
        dst[off++] = (uint8_t)label_len;
        memcpy(dst + off, p, label_len);
        off += label_len;
        if (!dot) break;
        p = dot + 1;
    }
    if (off + 1 > dstlen) return -1;
    dst[off++] = 0; // root label
    return off;
}

// Decode a (possibly compressed) name starting at buf[*poff].
// out may be NULL if the caller only wants to advance past the name.
// Returns 0 on success (and updates *poff to just after the name in the
// non-compressed sense), -1 on error.
static int hdns__decode_name(const uint8_t* buf, int buflen, int* poff,
                             char* out, int outlen) {
    int off = *poff;
    int outpos = 0;
    int jumps = 0;
    int advanced_off = -1; // offset to restore after first pointer jump

    while (1) {
        if (off < 0 || off >= buflen) return -1;
        uint8_t len = buf[off];
        if ((len & 0xC0) == 0xC0) {
            // compression pointer
            if (off + 1 >= buflen) return -1;
            if (advanced_off < 0) advanced_off = off + 2;
            int ptr = ((len & 0x3F) << 8) | buf[off + 1];
            off = ptr;
            if (++jumps > HDNS_MAX_LABEL_JUMPS) return -1;
            continue;
        }
        if ((len & 0xC0) != 0) return -1; // reserved bits set
        if (len == 0) {
            off += 1;
            break;
        }
        if (off + 1 + len > buflen) return -1;
        if (out) {
            if (outpos + len + 1 >= outlen) return -1;
            if (outpos > 0) out[outpos++] = '.';
            memcpy(out + outpos, buf + off + 1, len);
            outpos += len;
        }
        off += 1 + len;
    }
    if (out) out[outpos] = '\0';
    *poff = (advanced_off >= 0) ? advanced_off : off;
    return 0;
}

// Build a DNS query packet for (name, qtype) with the given transaction id.
// Returns packet length, or -1 on error.
static int hdns__build_query(uint16_t txid, const char* name, uint16_t qtype,
                             uint8_t* buf, int buflen) {
    if (buflen < HDNS_HDR_SIZE) return -1;
    memset(buf, 0, HDNS_HDR_SIZE);
    buf[0] = (uint8_t)(txid >> 8);
    buf[1] = (uint8_t)(txid & 0xFF);
    buf[2] = 0x01; // RD = 1 (recursion desired)
    // qdcount = 1
    buf[4] = 0x00;
    buf[5] = 0x01;

    int off = HDNS_HDR_SIZE;
    int namelen = hdns__encode_name(name, buf + off, buflen - off);
    if (namelen < 0) return -1;
    off += namelen;
    if (off + 4 > buflen) return -1;
    buf[off++] = (uint8_t)(qtype >> 8);
    buf[off++] = (uint8_t)(qtype & 0xFF);
    buf[off++] = 0x00; // class hi
    buf[off++] = HDNS_CLASS_IN;
    return off;
}

// Parse addresses (A/AAAA) out of a DNS response.
// @out_addrs / @max: caller-provided address array.
// @naddrs: number of addresses parsed (appended).
// @min_ttl: filled with the min TTL across accepted address records (seconds).
// Returns:  0 ok (addresses may be 0), HDNS_STATUS_* negative on failure.
static int hdns__parse_response(const uint8_t* buf, int buflen, uint16_t expect_txid,
                                sockaddr_u* out_addrs, int max, int* naddrs,
                                uint32_t* min_ttl) {
    if (buflen < HDNS_HDR_SIZE) return HDNS_STATUS_SERVFAIL;
    uint16_t txid = (buf[0] << 8) | buf[1];
    if (txid != expect_txid) return HDNS_STATUS_SERVFAIL;
    // must be a response (QR bit set), not a stray query echo
    if (!(buf[2] & 0x80)) return HDNS_STATUS_SERVFAIL;
    uint8_t flags2 = buf[3];
    int rcode = flags2 & 0x0F;
    if (rcode == 3) return HDNS_STATUS_NXDOMAIN;   // NXDOMAIN
    if (rcode != 0) return HDNS_STATUS_SERVFAIL;

    int qdcount = (buf[4] << 8) | buf[5];
    int ancount = (buf[6] << 8) | buf[7];
    // NOTE: never trust ancount for allocation; iterate defensively.

    int off = HDNS_HDR_SIZE;
    // skip questions
    for (int i = 0; i < qdcount; ++i) {
        if (hdns__decode_name(buf, buflen, &off, NULL, 0) != 0) return HDNS_STATUS_SERVFAIL;
        if (off + 4 > buflen) return HDNS_STATUS_SERVFAIL;
        off += 4; // qtype + qclass
    }

    uint32_t best_ttl = HDNS_CACHE_MAX_TTL;
    int count = *naddrs;
    for (int i = 0; i < ancount; ++i) {
        if (hdns__decode_name(buf, buflen, &off, NULL, 0) != 0) return HDNS_STATUS_SERVFAIL;
        if (off + 10 > buflen) return HDNS_STATUS_SERVFAIL;
        uint16_t rtype = (buf[off] << 8) | buf[off + 1];
        // uint16_t rclass = (buf[off+2] << 8) | buf[off+3];
        uint32_t ttl = ((uint32_t)buf[off + 4] << 24) | ((uint32_t)buf[off + 5] << 16) |
                       ((uint32_t)buf[off + 6] << 8) | (uint32_t)buf[off + 7];
        uint16_t rdlen = (buf[off + 8] << 8) | buf[off + 9];
        off += 10;
        if (off + rdlen > buflen) return HDNS_STATUS_SERVFAIL;

        if (rtype == HDNS_TYPE_A && rdlen == 4) {
            if (count < max) {
                sockaddr_u* a = &out_addrs[count];
                memset(a, 0, sizeof(*a));
                a->sin.sin_family = AF_INET;
                memcpy(&a->sin.sin_addr, buf + off, 4);
                ++count;
                if (ttl < best_ttl) best_ttl = ttl;
            }
        } else if (rtype == HDNS_TYPE_AAAA && rdlen == 16) {
            if (count < max) {
                sockaddr_u* a = &out_addrs[count];
                memset(a, 0, sizeof(*a));
                a->sin6.sin6_family = AF_INET6;
                memcpy(&a->sin6.sin6_addr, buf + off, 16);
                ++count;
                if (ttl < best_ttl) best_ttl = ttl;
            }
        }
        // CNAME and other records: skip rdata; the recursive resolver at the
        // nameserver has already followed CNAMEs and included A/AAAA in the
        // same answer section, so we only need to collect address records.
        off += rdlen;
    }

    *naddrs = count;
    if (min_ttl) *min_ttl = best_ttl;
    return HDNS_STATUS_OK;
}

//==============================================================================
// 2. Config: nameservers + hosts (process-wide, loaded lazily once)
//==============================================================================

typedef struct hdns_hosts_entry_s {
    char        name[HDNS_NAME_MAXLEN];
    sockaddr_u  addr;
    struct list_node node;
} hdns_hosts_entry_t;

static hmutex_t     s_config_mutex;
static honce_t      s_config_once = HONCE_INIT;
static int          s_config_loaded = 0;
static sockaddr_u   s_nameservers[HDNS_MAX_NAMESERVERS];
static int          s_nnameservers = 0;
static struct list_head s_hosts;    // list of hdns_hosts_entry_t

static void hdns__config_init_once(void) {
    hmutex_init(&s_config_mutex);
    list_init(&s_hosts);
}

static void hdns__config_lock_init(void) {
    // One-time, thread-safe init of the config mutex + hosts list.
    honce(&s_config_once, hdns__config_init_once);
}

static int hdns__parse_ns_ip(const char* ip, sockaddr_u* addr) {
    memset(addr, 0, sizeof(*addr));

    // Accept "ip", "ip:port" (IPv4) and "[ipv6]:port".
    char host[INET6_ADDRSTRLEN + 8] = {0};
    int port = HDNS_DEFAULT_PORT;
    if (ip[0] == '[') {
        // [ipv6]:port
        const char* end = strchr(ip, ']');
        if (!end) return -1;
        int hlen = (int)(end - ip - 1);
        if (hlen <= 0 || hlen >= (int)sizeof(host)) return -1;
        memcpy(host, ip + 1, hlen);
        host[hlen] = '\0';
        if (end[1] == ':') port = atoi(end + 2);
    } else {
        const char* colon = strchr(ip, ':');
        // exactly one colon => ipv4:port; more than one => bare IPv6 literal
        if (colon && strchr(colon + 1, ':') == NULL) {
            int hlen = (int)(colon - ip);
            if (hlen <= 0 || hlen >= (int)sizeof(host)) return -1;
            memcpy(host, ip, hlen);
            host[hlen] = '\0';
            port = atoi(colon + 1);
        } else {
            strncpy(host, ip, sizeof(host) - 1);
        }
    }
    if (port <= 0 || port > 65535) port = HDNS_DEFAULT_PORT;

    if (inet_pton(AF_INET, host, &addr->sin.sin_addr) == 1) {
        addr->sin.sin_family = AF_INET;
        addr->sin.sin_port = htons((uint16_t)port);
        return 0;
    }
    if (inet_pton(AF_INET6, host, &addr->sin6.sin6_addr) == 1) {
        addr->sin6.sin6_family = AF_INET6;
        addr->sin6.sin6_port = htons((uint16_t)port);
        return 0;
    }
    return -1;
}

static void hdns__add_nameserver(const char* ip) {
    if (s_nnameservers >= HDNS_MAX_NAMESERVERS) return;
    sockaddr_u addr;
    if (hdns__parse_ns_ip(ip, &addr) == 0) {
        s_nameservers[s_nnameservers++] = addr;
    }
}

#ifdef OS_WIN
static void hdns__load_nameservers_win(void) {
    ULONG buflen = 16 * 1024;
    IP_ADAPTER_ADDRESSES* addrs = (IP_ADAPTER_ADDRESSES*)hv_malloc(buflen);
    if (!addrs) return;
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                  GAA_FLAG_SKIP_FRIENDLY_NAME;
    ULONG ret = GetAdaptersAddresses(AF_UNSPEC, flags, NULL, addrs, &buflen);
    if (ret == ERROR_BUFFER_OVERFLOW) {
        hv_free(addrs);
        addrs = (IP_ADAPTER_ADDRESSES*)hv_malloc(buflen);
        if (!addrs) return;
        ret = GetAdaptersAddresses(AF_UNSPEC, flags, NULL, addrs, &buflen);
    }
    if (ret == NO_ERROR) {
        for (IP_ADAPTER_ADDRESSES* ad = addrs; ad; ad = ad->Next) {
            if (ad->OperStatus != IfOperStatusUp) continue;
            for (IP_ADAPTER_DNS_SERVER_ADDRESS* dns = ad->FirstDnsServerAddress;
                 dns; dns = dns->Next) {
                char ip[INET6_ADDRSTRLEN] = {0};
                struct sockaddr* sa = dns->Address.lpSockaddr;
                if (sa->sa_family == AF_INET) {
                    inet_ntop(AF_INET, &((struct sockaddr_in*)sa)->sin_addr, ip, sizeof(ip));
                } else if (sa->sa_family == AF_INET6) {
                    inet_ntop(AF_INET6, &((struct sockaddr_in6*)sa)->sin6_addr, ip, sizeof(ip));
                } else {
                    continue;
                }
                hdns__add_nameserver(ip);
                if (s_nnameservers >= HDNS_MAX_NAMESERVERS) break;
            }
            if (s_nnameservers >= HDNS_MAX_NAMESERVERS) break;
        }
    }
    hv_free(addrs);
}
#else
static void hdns__load_nameservers_unix(void) {
    FILE* fp = fopen("/etc/resolv.conf", "r");
    if (!fp) return;
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '#' || *p == ';') continue;
        if (strncmp(p, "nameserver", 10) != 0) continue;
        p += 10;
        if (*p != ' ' && *p != '\t') continue;
        while (*p == ' ' || *p == '\t') ++p;
        // isolate the ip token
        char ip[INET6_ADDRSTRLEN + 8] = {0};
        int i = 0;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' &&
               i < (int)sizeof(ip) - 1) {
            ip[i++] = *p++;
        }
        ip[i] = '\0';
        hdns__add_nameserver(ip);
        if (s_nnameservers >= HDNS_MAX_NAMESERVERS) break;
    }
    fclose(fp);
}
#endif

static void hdns__load_hosts(void) {
    const char* path;
#ifdef OS_WIN
    char winpath[MAX_PATH] = {0};
    const char* sysroot = getenv("SystemRoot");
    if (!sysroot) sysroot = "C:\\Windows";
    snprintf(winpath, sizeof(winpath), "%s\\System32\\drivers\\etc\\hosts", sysroot);
    path = winpath;
#else
    path = "/etc/hosts";
#endif
    FILE* fp = fopen(path, "r");
    if (!fp) return;
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        // strip comment
        char* hash = strchr(line, '#');
        if (hash) *hash = '\0';
        // first token = ip, remaining tokens = names
        char* save = NULL;
        char* ip = hdns_strtok_r(line, " \t\r\n", &save);
        if (!ip) continue;
        sockaddr_u addr;
        if (hdns__parse_ns_ip(ip, &addr) != 0) continue; // reuse: parses ip, sets port 53 (ignored)
        char* name;
        while ((name = hdns_strtok_r(NULL, " \t\r\n", &save)) != NULL) {
            hdns_hosts_entry_t* e;
            HV_ALLOC_SIZEOF(e);
            if (!e) continue;
            strncpy(e->name, name, sizeof(e->name) - 1);
            e->addr = addr;
            e->addr.sin.sin_port = 0; // hosts entries carry no port
            list_add(&e->node, &s_hosts);
        }
    }
    fclose(fp);
}

static void hdns__load_config_locked(void) {
    if (s_config_loaded) return;
    s_config_loaded = 1;
    s_nnameservers = 0;
#ifdef OS_WIN
    hdns__load_nameservers_win();
#else
    hdns__load_nameservers_unix();
#endif
    if (s_nnameservers == 0) {
        // universal fallback
        hdns__add_nameserver(HDNS_FALLBACK_NAMESERVER);
    }
    hdns__load_hosts();
}

// Look up @name in /etc/hosts. Returns number of matched addresses appended.
static int hdns__hosts_lookup(const char* name, hdns_family_e family,
                              sockaddr_u* out, int max) {
    int count = 0;
    struct list_node* node;
    list_for_each(node, &s_hosts) {
        hdns_hosts_entry_t* e = list_entry(node, hdns_hosts_entry_t, node);
        if (strcasecmp(e->name, name) != 0) continue;
        int fam = e->addr.sa.sa_family;
        if (fam == AF_INET  && !(family & HDNS_QUERY_A))    continue;
        if (fam == AF_INET6 && !(family & HDNS_QUERY_AAAA)) continue;
        if (count < max) out[count++] = e->addr;
    }
    return count;
}

//==============================================================================
// 3/4. Per-loop resolver + cache
//==============================================================================

typedef struct hdns_cache_entry_s {
    char        host[HDNS_NAME_MAXLEN];
    int         family;                     // hdns_family_e bitset
    int         naddrs;
    sockaddr_u  addrs[HDNS_MAX_ADDRS];
    uint64_t    expire_ms;                  // loop clock ms
    int         negative;                   // 1 = cached failure
    struct list_node node;
} hdns_cache_entry_t;

// One logical resolve.
// NOTE: hdns_s is a subclass of hevent_t (like htimer_s / hio_s): it starts
// with HEVENT_FIELDS so it inherits `loop`, `event_id`, `userdata`, `priority`
// and the destroy/active/pending flags, and can use hevent_set_id/hevent_id.
// It is NOT scheduled by the loop as an event, though; it is driven by the
// resolver's UDP io + timers. HEVENT_FIELDS is reused only for a uniform
// layout and a built-in id slot.
struct hdns_s {
    HEVENT_FIELDS   // loop, event_type, event_id, cb, userdata, priority, flags...
    // extra 1-bit flags packed right after HEVENT_FLAGS (destroy/active/pending)
    // to share the same storage unit and save space.
    unsigned        detached   : 1; // cancelled: run to completion, drop result
    unsigned        delivering : 1; // inside hdns__deliver's callback (cancel guard)
    struct hdns_resolver_s* resolver;
    char            host[HDNS_NAME_MAXLEN];
    hdns_setting_t  opt;
    hdns_cb         dns_cb;         // user callback (hevent_t::cb has a different type)

    // sub-queries: index 0 = A, index 1 = AAAA (per opt.family)
    struct {
        int         active;         // waiting for a response
        int         done;           // settled (answer/empty/error)
        uint16_t    txid;
        uint16_t    qtype;
    } sub[2];

    int             attempt;        // current attempt (0..retries)
    int             ns_index;       // current nameserver index
    htimer_t*       timer;          // per-attempt timeout timer
    htimer_t*       defer_timer;    // for async delivery of immediate results

    // accumulated results
    sockaddr_u      addrs[HDNS_MAX_ADDRS];
    int             naddrs;
    uint32_t        min_ttl;
    int             any_error;      // last non-timeout error seen

    struct list_node node;          // in resolver->queries
};

typedef struct hdns_resolver_s {
    hloop_t*        loop;
    hio_t*          io4;            // UDP socket for IPv4 nameservers (lazy)
    hio_t*          io6;            // UDP socket for IPv6 nameservers (lazy)
    struct list_head queries;       // in-flight hdns_t
    struct list_head cache;         // hdns_cache_entry_t (LRU-ish by insertion)
    int             ncache;
    uint16_t        next_txid;      // monotonic base for sub-query txids (wire)
    uint8_t         sndbuf[HDNS_UDP_BUFSIZE];
} hdns_resolver_t;

// forward decls
static void hdns__on_udp_read(hio_t* io, void* buf, int readbytes);
static void hdns__on_timeout(htimer_t* timer);
static void hdns__on_defer(htimer_t* timer);
static void hdns__send_queries(hdns_t* q);
static void hdns__finish(hdns_t* q, int status);

// Lazily create the UDP send/recv socket matching the nameserver's family.
// A socket's family must match sendto()'s destination, so IPv4 and IPv6
// nameservers need separate sockets. Returns NULL on failure.
static hio_t* hdns__resolver_io(hdns_resolver_t* r, int family) {
    hio_t** pio = (family == AF_INET6) ? &r->io6 : &r->io4;
    if (*pio) return *pio;
    int fd = socket(family, SOCK_DGRAM, 0);
    if (fd < 0) return NULL;
    hio_t* io = hio_get(r->loop, fd);
    if (!io) {
        closesocket(fd);
        return NULL;
    }
    hio_set_type(io, HIO_TYPE_UDP);
    hio_setcb_read(io, hdns__on_udp_read);
    hio_set_context(io, r);
    hio_read(io);
    *pio = io;
    return io;
}

static hdns_resolver_t* hdns__resolver_get(hloop_t* loop) {
    hdns_resolver_t* r = (hdns_resolver_t*)loop->dns_resolver;
    if (r) return r;

    HV_ALLOC_SIZEOF(r);
    if (!r) return NULL;
    r->loop = loop;
    list_init(&r->queries);
    list_init(&r->cache);
    r->ncache = 0;
    // seed the txid counter randomly to reduce off-path spoofing predictability.
    r->next_txid = (uint16_t)hv_rand(1, 0xFFFF);
    // sockets are created lazily per nameserver family in hdns__resolver_io().

    loop->dns_resolver = r;
    return r;
}

void hdns_resolver_free(hloop_t* loop) {
    hdns_resolver_t* r = (hdns_resolver_t*)loop->dns_resolver;
    if (!r) return;
    loop->dns_resolver = NULL;

    // cancel outstanding queries (no callbacks on teardown)
    struct list_node *node, *tmp;
    list_for_each_safe(node, tmp, &r->queries) {
        hdns_t* q = list_entry(node, hdns_t, node);
        list_del(&q->node);
        if (q->timer) htimer_del(q->timer);
        if (q->defer_timer) htimer_del(q->defer_timer);
        HV_FREE(q);
    }
    // free cache
    list_for_each_safe(node, tmp, &r->cache) {
        hdns_cache_entry_t* e = list_entry(node, hdns_cache_entry_t, node);
        list_del(&e->node);
        HV_FREE(e);
    }
    // NOTE: r->io4/io6 are owned by the loop and freed by hloop_cleanup's io sweep.
    HV_FREE(r);
}

//---------------------------- cache ------------------------------------------

static hdns_cache_entry_t* hdns__cache_find(hdns_resolver_t* r, const char* host,
                                            int family) {
    uint64_t now = hloop_now_ms(r->loop);
    struct list_node *node, *tmp;
    list_for_each_safe(node, tmp, &r->cache) {
        hdns_cache_entry_t* e = list_entry(node, hdns_cache_entry_t, node);
        if (now >= e->expire_ms) {
            // expired -> evict
            list_del(&e->node);
            HV_FREE(e);
            --r->ncache;
            continue;
        }
        if (e->family == family && strcasecmp(e->host, host) == 0) {
            return e;
        }
    }
    return NULL;
}

static void hdns__cache_put(hdns_resolver_t* r, const char* host, int family,
                            const sockaddr_u* addrs, int naddrs,
                            uint32_t ttl_sec, int negative) {
    // drop existing entry for same key
    struct list_node *node, *tmp;
    list_for_each_safe(node, tmp, &r->cache) {
        hdns_cache_entry_t* e = list_entry(node, hdns_cache_entry_t, node);
        if (e->family == family && strcasecmp(e->host, host) == 0) {
            list_del(&e->node);
            HV_FREE(e);
            --r->ncache;
            break;
        }
    }
    // bound cache size: evict oldest (list tail) when full
    if (r->ncache >= HDNS_CACHE_MAX_ENTRIES && !list_empty(&r->cache)) {
        hdns_cache_entry_t* old = list_entry(r->cache.prev, hdns_cache_entry_t, node);
        list_del(&old->node);
        HV_FREE(old);
        --r->ncache;
    }

    hdns_cache_entry_t* e;
    HV_ALLOC_SIZEOF(e);
    if (!e) return;
    strncpy(e->host, host, sizeof(e->host) - 1);
    e->family = family;
    e->negative = negative;
    e->naddrs = 0;
    if (!negative && addrs && naddrs > 0) {
        e->naddrs = naddrs > HDNS_MAX_ADDRS ? HDNS_MAX_ADDRS : naddrs;
        memcpy(e->addrs, addrs, e->naddrs * sizeof(sockaddr_u));
    }
    if (ttl_sec < HDNS_CACHE_MIN_TTL) ttl_sec = HDNS_CACHE_MIN_TTL;
    if (ttl_sec > HDNS_CACHE_MAX_TTL) ttl_sec = HDNS_CACHE_MAX_TTL;
    e->expire_ms = hloop_now_ms(r->loop) + (uint64_t)ttl_sec * 1000;
    list_add(&e->node, &r->cache); // newest at head
    ++r->ncache;
}

void hdns_clear_cache(hloop_t* loop) {
    hdns_resolver_t* r = (hdns_resolver_t*)loop->dns_resolver;
    if (!r) return;
    struct list_node *node, *tmp;
    list_for_each_safe(node, tmp, &r->cache) {
        hdns_cache_entry_t* e = list_entry(node, hdns_cache_entry_t, node);
        list_del(&e->node);
        HV_FREE(e);
    }
    r->ncache = 0;
}

//==============================================================================
// 5. Query lifecycle
//==============================================================================

// Fill an hdns_result_t and invoke the user callback, then free the query.
// A cancelled (detached) query is freed silently without invoking any callback.
static void hdns__deliver(hdns_t* q, int status) {
    hdns_cb cb = q->detached ? NULL : q->dns_cb;
    void* ud = q->userdata;

    hdns_result_t result;
    if (cb) {
        memset(&result, 0, sizeof(result));
        result.status = status;
        strncpy(result.host, q->host, sizeof(result.host) - 1);
        if (status == HDNS_STATUS_OK) {
            result.naddrs = q->naddrs > HDNS_MAX_ADDRS ? HDNS_MAX_ADDRS : q->naddrs;
            memcpy(result.addrs, q->addrs, result.naddrs * sizeof(sockaddr_u));
        }
    }

    // Detach from the resolver and stop timers, then invoke the callback while
    // the handle is still alive (the cb receives it, e.g. to read hevent_id),
    // and finally free it.
    // NOTE: the public API forbids calling hdns_cancel(q) from within q's own
    // callback. hdns_cancel() has a re-entrancy guard (see below) so such misuse
    // fails safe rather than double-freeing, but callers must not rely on it.
    if (q->node.next) { list_del(&q->node); q->node.next = q->node.prev = NULL; }
    if (q->timer) { htimer_del(q->timer); q->timer = NULL; }
    if (q->defer_timer) { htimer_del(q->defer_timer); q->defer_timer = NULL; }

    q->delivering = 1;
    if (cb) cb(q, &result, ud);

    HV_FREE(q);
}

// Sort accumulated addresses: IPv4 first (stable), matching getaddrinfo-ish order.
static void hdns__sort_v4_first(hdns_t* q) {
    sockaddr_u tmp[HDNS_MAX_ADDRS];
    int n = 0;
    for (int i = 0; i < q->naddrs; ++i) {
        if (q->addrs[i].sa.sa_family == AF_INET) tmp[n++] = q->addrs[i];
    }
    for (int i = 0; i < q->naddrs; ++i) {
        if (q->addrs[i].sa.sa_family != AF_INET) tmp[n++] = q->addrs[i];
    }
    memcpy(q->addrs, tmp, n * sizeof(sockaddr_u));
    q->naddrs = n;
}

// Called when all requested sub-queries have settled.
static void hdns__complete(hdns_t* q) {
    hdns_resolver_t* r = q->resolver;
    int status;
    if (q->naddrs > 0) {
        hdns__sort_v4_first(q);
        status = HDNS_STATUS_OK;
        if (q->opt.use_cache) {
            hdns__cache_put(r, q->host, q->opt.family, q->addrs, q->naddrs,
                            q->min_ttl, 0);
        }
    } else {
        status = q->any_error ? q->any_error : HDNS_STATUS_NXDOMAIN;
        if (q->opt.use_cache && status == HDNS_STATUS_NXDOMAIN) {
            hdns__cache_put(r, q->host, q->opt.family, NULL, 0,
                            HDNS_CACHE_NEG_TTL, 1);
        }
    }
    hdns__deliver(q, status);
}

static int hdns__all_done(hdns_t* q) {
    for (int i = 0; i < 2; ++i) {
        if (q->sub[i].qtype && !q->sub[i].done) return 0;
    }
    return 1;
}

// Send (or resend) all not-yet-done sub-queries to the current nameserver.
static void hdns__send_queries(hdns_t* q) {
    hdns_resolver_t* r = q->resolver;
    hdns__config_lock_init();
    hmutex_lock(&s_config_mutex);
    hdns__load_config_locked();

    sockaddr_u ns;
    if (q->opt.nameserver) {
        if (hdns__parse_ns_ip(q->opt.nameserver, &ns) != 0) {
            hmutex_unlock(&s_config_mutex);
            hdns__finish(q, HDNS_STATUS_NONAMESERVER);
            return;
        }
    } else {
        if (s_nnameservers == 0) {
            hmutex_unlock(&s_config_mutex);
            hdns__finish(q, HDNS_STATUS_NONAMESERVER);
            return;
        }
        ns = s_nameservers[q->ns_index % s_nnameservers];
    }
    hmutex_unlock(&s_config_mutex);

    // pick the UDP socket matching the nameserver's address family.
    hio_t* io = hdns__resolver_io(r, ns.sa.sa_family);
    if (io == NULL) {
        hdns__finish(q, HDNS_STATUS_NONAMESERVER);
        return;
    }

    for (int i = 0; i < 2; ++i) {
        if (!q->sub[i].qtype || q->sub[i].done) continue;
        int len = hdns__build_query(q->sub[i].txid, q->host, q->sub[i].qtype,
                                    r->sndbuf, sizeof(r->sndbuf));
        if (len < 0) {
            q->sub[i].done = 1;
            q->any_error = HDNS_STATUS_BADNAME;
            continue;
        }
        q->sub[i].active = 1;
        hio_sendto(io, r->sndbuf, len, &ns.sa);
    }

    if (hdns__all_done(q)) {
        // build failures for all sub-queries
        hdns__complete(q);
        return;
    }

    // (re)arm per-attempt timeout
    if (q->timer) { htimer_del(q->timer); q->timer = NULL; }
    q->timer = htimer_add(r->loop, hdns__on_timeout, q->opt.timeout_ms, 1);
    if (q->timer) hevent_set_userdata(q->timer, q);
}

// Per-attempt timeout: retry (rotate nameserver) or give up.
static void hdns__on_timeout(htimer_t* timer) {
    hdns_t* q = (hdns_t*)hevent_userdata(timer);
    if (!q) return;
    q->timer = NULL; // this single-shot timer auto-deletes after firing

    if (q->attempt < q->opt.retries) {
        ++q->attempt;
        ++q->ns_index; // rotate to next nameserver
        hdns__send_queries(q);
    } else {
        // mark unfinished sub-queries as timed out
        for (int i = 0; i < 2; ++i) {
            if (q->sub[i].qtype && !q->sub[i].done) {
                q->sub[i].done = 1;
                q->sub[i].active = 0;
                if (!q->any_error) q->any_error = HDNS_STATUS_TIMEOUT;
            }
        }
        hdns__complete(q);
    }
}

// Terminal helper for setup errors (async-safe: schedules delivery).
static void hdns__finish(hdns_t* q, int status) {
    q->naddrs = 0;
    q->any_error = status;
    hdns__deliver(q, status);
}

// UDP read: demux by transaction id to the matching sub-query.
static void hdns__on_udp_read(hio_t* io, void* buf, int readbytes) {
    hdns_resolver_t* r = (hdns_resolver_t*)hio_context(io);
    if (!r || readbytes < HDNS_HDR_SIZE) return;
    const uint8_t* pkt = (const uint8_t*)buf;
    uint16_t txid = (pkt[0] << 8) | pkt[1];

    // find the query + sub-query for this txid
    struct list_node* node;
    list_for_each(node, &r->queries) {
        hdns_t* q = list_entry(node, hdns_t, node);
        for (int i = 0; i < 2; ++i) {
            if (!q->sub[i].qtype || q->sub[i].done) continue;
            if (q->sub[i].txid != txid) continue;

            // parse this sub-query's answer
            uint32_t ttl = HDNS_CACHE_MAX_TTL;
            int rc = hdns__parse_response(pkt, readbytes, txid,
                                          q->addrs, HDNS_MAX_ADDRS,
                                          &q->naddrs, &ttl);
            if (rc == HDNS_STATUS_OK) {
                if (ttl < q->min_ttl) q->min_ttl = ttl;
            } else if (rc == HDNS_STATUS_NXDOMAIN) {
                if (!q->any_error) q->any_error = HDNS_STATUS_NXDOMAIN;
            } else {
                if (!q->any_error) q->any_error = HDNS_STATUS_SERVFAIL;
            }
            q->sub[i].done = 1;
            q->sub[i].active = 0;

            if (hdns__all_done(q)) {
                hdns__complete(q);
            }
            return;
        }
    }
    // unknown txid: stale/duplicate response, ignore.
}

// Deferred delivery for immediate results (numeric IP / hosts / cache hit).
static void hdns__on_defer(htimer_t* timer) {
    hdns_t* q = (hdns_t*)hevent_userdata(timer);
    if (!q) return;
    q->defer_timer = NULL; // single-shot auto-deletes
    hdns__complete(q);
}

// Deferred start of the network query, so hdns_resolve() never sends (or
// completes, or invokes the callback) re-entrantly inside the call.
static void hdns__on_start(htimer_t* timer) {
    hdns_t* q = (hdns_t*)hevent_userdata(timer);
    if (!q) return;
    q->defer_timer = NULL; // single-shot auto-deletes
    hdns__send_queries(q);
}

static void hdns__defer_complete(hdns_t* q) {
    // schedule completion on the next loop tick (1ms) so cb never runs
    // re-entrantly inside hdns_resolve().
    q->defer_timer = htimer_add(q->loop, hdns__on_defer, 1, 1);
    if (q->defer_timer) {
        hevent_set_userdata(q->defer_timer, q);
    } else {
        // extremely unlikely; deliver synchronously as a last resort
        hdns__complete(q);
    }
}

// Is @txid currently used by any in-flight (not-yet-done) sub-query?
static int hdns__txid_in_use(hdns_resolver_t* r, uint16_t txid) {
    struct list_node* node;
    list_for_each(node, &r->queries) {
        hdns_t* q = list_entry(node, hdns_t, node);
        for (int i = 0; i < 2; ++i) {
            if (q->sub[i].qtype && !q->sub[i].done && q->sub[i].txid == txid) {
                return 1;
            }
        }
    }
    return 0;
}

// Allocate a (base, base+1) txid pair not currently in flight. Starts from a
// per-resolver monotonic counter and probes forward on collision. Bounded to
// avoid an infinite loop in the pathological case of >32K live sub-queries.
static uint16_t hdns__alloc_txid_pair(hdns_resolver_t* r) {
    for (int tries = 0; tries < 0x8000; ++tries) {
        uint16_t base = r->next_txid;
        r->next_txid += 2;
        if (!hdns__txid_in_use(r, base) &&
            !hdns__txid_in_use(r, (uint16_t)(base + 1))) {
            return base;
        }
    }
    // extremely unlikely fallback: return the current counter as-is.
    uint16_t base = r->next_txid;
    r->next_txid += 2;
    return base;
}

hdns_t* hdns_resolve_ex(hloop_t* loop, const char* host,
                        const hdns_setting_t* opt,
                        hdns_cb cb, void* userdata) {
    if (!loop || !host || !cb) return NULL;
    size_t hlen = strlen(host);
    if (hlen == 0 || hlen >= HDNS_NAME_MAXLEN) return NULL;

    hdns_resolver_t* r = hdns__resolver_get(loop);
    if (!r) return NULL;

    hdns_t* q;
    HV_ALLOC_SIZEOF(q);
    if (!q) return NULL;
    q->loop = loop;
    q->event_type = HEVENT_TYPE_CUSTOM;
    q->resolver = r;
    strncpy(q->host, host, sizeof(q->host) - 1);
    q->dns_cb = cb;
    q->userdata = userdata;
    q->min_ttl = HDNS_CACHE_MAX_TTL;

    // options with defaults
    if (opt) {
        q->opt = *opt;
    } else {
        q->opt.family = HDNS_QUERY_BOTH;
        q->opt.timeout_ms = HDNS_DEFAULT_TIMEOUT_MS;
        q->opt.retries = HDNS_DEFAULT_RETRIES;
        q->opt.use_cache = 1;
        q->opt.nameserver = NULL;
    }
    if (q->opt.family == 0) q->opt.family = HDNS_QUERY_BOTH;
    if (q->opt.timeout_ms <= 0) q->opt.timeout_ms = HDNS_DEFAULT_TIMEOUT_MS;
    if (q->opt.retries < 0) q->opt.retries = 0;

    // register in resolver so it can be cancelled / matched
    list_add(&q->node, &r->queries);

    // NOTE: htimer_add(loop, cb, 1, 1) below never returns NULL (timeout_ms>0),
    // so completion is always deferred to the next loop tick and this function
    // always returns a still-valid handle before any callback runs.

    // 1) numeric IP fast path
    sockaddr_u num;
    memset(&num, 0, sizeof(num));
    if (inet_pton(AF_INET, host, &num.sin.sin_addr) == 1) {
        num.sin.sin_family = AF_INET;
        q->addrs[q->naddrs++] = num;
        hdns__defer_complete(q);
        return q;
    }
    if (inet_pton(AF_INET6, host, &num.sin6.sin6_addr) == 1) {
        num.sin6.sin6_family = AF_INET6;
        q->addrs[q->naddrs++] = num;
        hdns__defer_complete(q);
        return q;
    }

    // 2) hosts + cache lookups (need config loaded)
    hdns__config_lock_init();
    hmutex_lock(&s_config_mutex);
    hdns__load_config_locked();
    int nhosts = hdns__hosts_lookup(host, q->opt.family, q->addrs, HDNS_MAX_ADDRS);
    hmutex_unlock(&s_config_mutex);
    if (nhosts > 0) {
        q->naddrs = nhosts;
        hdns__defer_complete(q);
        return q;
    }

    if (q->opt.use_cache) {
        hdns_cache_entry_t* ce = hdns__cache_find(r, host, q->opt.family);
        if (ce) {
            if (ce->negative) {
                q->any_error = HDNS_STATUS_NXDOMAIN;
            } else {
                q->naddrs = ce->naddrs;
                memcpy(q->addrs, ce->addrs, ce->naddrs * sizeof(sockaddr_u));
            }
            hdns__defer_complete(q);
            return q;
        }
    }

    // 3) network query: assign sub-queries + txids, and start on the next loop
    //    tick so this function returns the handle before any send/callback.
    //    Allocate a (base, base+1) txid pair that is NOT currently used by any
    //    in-flight sub-query, so 16-bit wrap can never mis-deliver a response to
    //    a live query. The in-flight set is tiny, so a bounded probe is cheap.
    uint16_t base = hdns__alloc_txid_pair(r);
    if (q->opt.family & HDNS_QUERY_A) {
        q->sub[0].qtype = HDNS_TYPE_A;
        q->sub[0].txid = base;
    }
    if (q->opt.family & HDNS_QUERY_AAAA) {
        q->sub[1].qtype = HDNS_TYPE_AAAA;
        q->sub[1].txid = (uint16_t)(base + 1);
    }
    q->defer_timer = htimer_add(loop, hdns__on_start, 1, 1);
    if (q->defer_timer) {
        hevent_set_userdata(q->defer_timer, q);
    } else {
        // extremely unlikely; start synchronously as a last resort
        hdns__send_queries(q);
    }
    return q;
}

hdns_t* hdns_resolve(hloop_t* loop, const char* host,
                     hdns_cb cb, void* userdata) {
    return hdns_resolve_ex(loop, host, NULL, cb, userdata);
}

void hdns_cancel(hdns_t* q) {
    if (!q) return;
    // Fail-safe against the forbidden re-entrant case: if q is currently being
    // delivered (cancel called from within its own callback), hdns__deliver
    // will free it right after the callback returns, so do nothing here to
    // avoid a double-free.
    if (q->delivering) return;
    if (q->node.next) list_del(&q->node);
    if (q->timer) { htimer_del(q->timer); q->timer = NULL; }
    if (q->defer_timer) { htimer_del(q->defer_timer); q->defer_timer = NULL; }
    HV_FREE(q);
}
