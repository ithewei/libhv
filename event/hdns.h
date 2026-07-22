#ifndef HV_DNS_ASYNC_H_
#define HV_DNS_ASYNC_H_

/*
 * Asynchronous DNS resolver.
 *
 * A self-contained, non-blocking, native UDP DNS resolver that runs entirely
 * inside the hloop event loop. Unlike the blocking getaddrinfo() used by
 * ResolveAddr(), hdns_resolve() never stalls the event loop: DNS queries are
 * sent over a non-blocking UDP socket and results are delivered through a
 * callback in the loop thread.
 *
 * NOTE: This is independent from protocol/dns.h (which is a synchronous demo).
 *
 * Features:
 *   - Async A (IPv4) / AAAA (IPv6) queries.
 *   - Reads nameservers from /etc/resolv.conf (Unix) or GetAdaptersAddresses (Windows),
 *     falling back to 8.8.8.8.
 *   - Loads /etc/hosts and answers from it before querying the network.
 *   - Numeric-IP fast path (no query for literal IPv4/IPv6).
 *   - Per-query timeout, retry and multi-nameserver rotation.
 *   - DNS name compression parsing and CNAME chain following.
 *   - TTL-respecting per-loop cache (positive + negative).
 *   - Cancelable query handle.
 *
 * @see examples/hdns_example.c
 */

#include "hexport.h"
#include "hplatform.h"
#include "hsocket.h" // sockaddr_u
#include "hloop.h"   // hloop_t

#define HDNS_DEFAULT_PORT           53
#define HDNS_DEFAULT_TIMEOUT_MS     5000
#define HDNS_DEFAULT_RETRIES        2       // total attempts = retries + 1
#define HDNS_FALLBACK_NAMESERVER    "8.8.8.8"
#define HDNS_MAX_ADDRS              16
#define HDNS_NAME_MAXLEN           256

// hdns_result_t.status codes (0 = success, negative = failure)
#define HDNS_STATUS_OK              0
#define HDNS_STATUS_TIMEOUT       (-1)   // all attempts timed out
#define HDNS_STATUS_NXDOMAIN      (-2)   // no such domain / no address record
#define HDNS_STATUS_SERVFAIL      (-3)   // server failure / bad response
#define HDNS_STATUS_BADNAME       (-4)   // invalid host name
#define HDNS_STATUS_NONAMESERVER  (-5)   // no usable nameserver
#define HDNS_STATUS_NOMEM         (-6)   // out of memory
#define HDNS_STATUS_CANCELLED     (-7)   // query cancelled (not delivered to cb)
#define HDNS_STATUS_ERROR         (-8)   // other error

typedef enum {
    HDNS_QUERY_A    = 0x01,     // IPv4 only
    HDNS_QUERY_AAAA = 0x02,     // IPv6 only
    HDNS_QUERY_BOTH = 0x03,     // A + AAAA (default)
} hdns_family_e;

typedef struct hdns_options_s {
    hdns_family_e   family;     // default HDNS_QUERY_BOTH
    int             timeout_ms; // per-attempt timeout, default HDNS_DEFAULT_TIMEOUT_MS
    int             retries;    // default HDNS_DEFAULT_RETRIES
    int             use_cache;  // default 1
    const char*     nameserver; // optional override "ip" or "ip:port", NULL = auto

#ifdef __cplusplus
    hdns_options_s() {
        family = HDNS_QUERY_BOTH;
        timeout_ms = HDNS_DEFAULT_TIMEOUT_MS;
        retries = HDNS_DEFAULT_RETRIES;
        use_cache = 1;
        nameserver = NULL;
    }
#endif
} hdns_options_t;

typedef struct hdns_result_s {
    int         status;                     // 0:ok  <0:herr code
    char        host[HDNS_NAME_MAXLEN];     // queried name
    int         naddrs;                     // number of resolved addresses
    sockaddr_u  addrs[HDNS_MAX_ADDRS];      // A/AAAA merged (IPv4 first), port = 0
} hdns_result_t;

// opaque, cancelable handle
typedef struct hdns_query_s hdns_query_t;

/*
 * result callback, invoked in the loop thread.
 * NOTE: the result pointer is only valid during the callback.
 */
typedef void (*hdns_cb)(const hdns_result_t* result, void* userdata);

BEGIN_EXTERN_C

/*
 * Start an asynchronous resolve bound to @loop. @cb runs in the loop thread.
 *
 * The callback is NEVER invoked re-entrantly inside this call: even for
 * numeric-IP / hosts / cache hits, completion is posted to the next loop
 * iteration. Therefore the returned handle is always valid to pass to
 * hdns_cancel() until the callback fires.
 *
 * @return a query handle, or NULL on immediate failure (invalid params / OOM).
 */
HV_EXPORT hdns_query_t* hdns_resolve(hloop_t* loop, const char* host,
                                     hdns_cb cb, void* userdata);

HV_EXPORT hdns_query_t* hdns_resolve_ex(hloop_t* loop, const char* host,
                                        const hdns_options_t* opt,
                                        hdns_cb cb, void* userdata);

/*
 * Cancel an in-flight query. After this returns, @cb will NOT be called and
 * @query is invalid. MUST be called from the loop thread.
 */
HV_EXPORT void hdns_cancel(hdns_query_t* query);

// Clear the per-loop DNS cache (e.g. on a network change). Loop-thread only.
HV_EXPORT void hdns_clear_cache(hloop_t* loop);

END_EXTERN_C

#endif // HV_DNS_ASYNC_H_
