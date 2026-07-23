/*
 * hdns_test — unit test for the asynchronous DNS resolver (event/hdns.*).
 *
 * Tests run deterministically without requiring external network access by
 * spinning up a local mock DNS nameserver (a UDP socket answering with canned
 * A records) and pointing the resolver at it via hdns_options_t.nameserver.
 *
 * Covered:
 *   1. numeric IPv4 / IPv6 fast path
 *   2. /etc/hosts lookup (localhost)
 *   3. real query round-trip against a mock nameserver (wire build + parse)
 *   4. cache hit (second resolve does not hit the mock server)
 *   5. cancel before completion (callback not invoked)
 *   6. NXDOMAIN handling
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "hloop.h"
#include "hdns.h"
#include "hsocket.h"
#include "hbase.h"
#include "htime.h"

//------------------------------------------------------------------------------
// Mock DNS nameserver: parses the incoming question and replies with A records
// based on a tiny built-in zone. Runs in the same loop.
//------------------------------------------------------------------------------

static int g_mock_queries = 0;   // count of questions received by the mock
static int g_mock_port = 0;

// Build a minimal DNS response for the given request buffer.
// Answers with 93.184.216.34 for any A query except "nx.test" (NXDOMAIN).
static int mock_build_response(const uint8_t* req, int reqlen, uint8_t* resp, int resplen) {
    if (reqlen < 12) return -1;
    // copy header, set QR=1, RA=1
    memcpy(resp, req, 12);
    resp[2] = 0x81; // QR=1, RD=1
    resp[3] = 0x80; // RA=1, rcode=0

    // parse question name to detect nx.test
    int off = 12;
    char name[256] = {0};
    int npos = 0;
    while (off < reqlen) {
        uint8_t len = req[off];
        if (len == 0) { off += 1; break; }
        if ((len & 0xC0) != 0) return -1;
        if (off + 1 + len > reqlen) return -1;
        if (npos > 0) name[npos++] = '.';
        memcpy(name + npos, req + off + 1, len);
        npos += len;
        off += 1 + len;
    }
    if (off + 4 > reqlen) return -1;
    uint16_t qtype = (req[off] << 8) | req[off + 1];
    off += 4; // qtype + qclass
    int qend = off;

    int is_nx = (strcasecmp(name, "nx.test") == 0);
    int answer_a = (qtype == 1 && !is_nx); // only answer A here

    // counts
    resp[4] = req[4]; resp[5] = req[5];  // qdcount = 1
    if (is_nx) {
        resp[3] = 0x83; // rcode = 3 NXDOMAIN
        resp[6] = 0; resp[7] = 0;
    } else {
        resp[6] = 0; resp[7] = (uint8_t)(answer_a ? 1 : 0); // ancount
    }
    resp[8] = 0; resp[9] = 0;
    resp[10] = 0; resp[11] = 0;

    // copy question section verbatim
    if (qend > resplen) return -1;
    memcpy(resp + 12, req + 12, qend - 12);
    int roff = qend;

    if (answer_a) {
        // answer: name pointer -> 0xC00C, type A, class IN, ttl 60, rdlen 4
        if (roff + 16 > resplen) return -1;
        resp[roff++] = 0xC0; resp[roff++] = 0x0C; // ptr to qname at offset 12
        resp[roff++] = 0x00; resp[roff++] = 0x01; // type A
        resp[roff++] = 0x00; resp[roff++] = 0x01; // class IN
        resp[roff++] = 0x00; resp[roff++] = 0x00;
        resp[roff++] = 0x00; resp[roff++] = 0x3C; // ttl 60
        resp[roff++] = 0x00; resp[roff++] = 0x04; // rdlen 4
        resp[roff++] = 93;  resp[roff++] = 184;
        resp[roff++] = 216; resp[roff++] = 34;
    }
    return roff;
}

static void mock_on_recv(hio_t* io, void* buf, int readbytes) {
    ++g_mock_queries;
    uint8_t resp[512];
    int rlen = mock_build_response((const uint8_t*)buf, readbytes, resp, sizeof(resp));
    if (rlen > 0) {
        // reply to sender (peeraddr was set by recvfrom)
        hio_sendto(io, resp, rlen, hio_peeraddr(io));
    }
}

static hio_t* start_mock_nameserver(hloop_t* loop) {
    // bind an ephemeral UDP port on 127.0.0.1
    hio_t* io = hloop_create_udp_server(loop, "127.0.0.1", 0);
    assert(io != NULL);
    struct sockaddr* la = hio_localaddr(io);
    g_mock_port = ntohs(((struct sockaddr_in*)la)->sin_port);
    hio_setcb_read(io, mock_on_recv);
    hio_read(io);
    return io;
}

//------------------------------------------------------------------------------
// Test harness: each test sets an expectation and stops the loop when met.
//------------------------------------------------------------------------------

typedef struct {
    const char* label;
    int         got;
    int         want_status;
    int         want_min_addrs;
    hloop_t*    loop;
} expect_t;

static void on_expect(hdns_t* query, const hdns_result_t* result, void* userdata) {
    (void)query;
    expect_t* e = (expect_t*)userdata;
    e->got = 1;
    printf("[%s] status=%d naddrs=%d\n", e->label, result->status, result->naddrs);
    assert(result->status == e->want_status);
    if (result->status == HDNS_STATUS_OK) {
        assert(result->naddrs >= e->want_min_addrs);
    }
    hloop_stop(e->loop);
}

static void run_expect(hloop_t* loop, const char* host, const hdns_options_t* opt,
                       int want_status, int want_min_addrs) {
    expect_t e;
    memset(&e, 0, sizeof(e));
    e.label = host;
    e.want_status = want_status;
    e.want_min_addrs = want_min_addrs;
    e.loop = loop;
    hdns_t* q = hdns_resolve_ex(loop, host, opt, on_expect, &e);
    assert(q != NULL);
    hloop_run(loop);
    assert(e.got == 1);
}

// callback that must NEVER be invoked (cancel test)
static int g_cancel_cb_called = 0;
static void on_never(hdns_t* query, const hdns_result_t* result, void* userdata) {
    (void)query; (void)result; (void)userdata;
    g_cancel_cb_called = 1;
}
static void stop_after(htimer_t* timer) {
    hloop_stop(hevent_loop(timer));
}

int main() {
    hloop_t* loop = hloop_new(HLOOP_FLAG_QUIT_WHEN_NO_ACTIVE_EVENTS == 0 ? 0 : 0);

    hio_t* mock = start_mock_nameserver(loop);
    char ns[32];
    snprintf(ns, sizeof(ns), "127.0.0.1:%d", g_mock_port);
    printf("mock nameserver on %s\n", ns);

    hdns_options_t opt;
    memset(&opt, 0, sizeof(opt));
    opt.family = HDNS_QUERY_A;   // only A (mock answers A)
    opt.timeout_ms = 2000;
    opt.retries = 1;
    opt.use_cache = 1;
    opt.nameserver = ns;

    // 1) numeric IPv4 fast path (no nameserver needed)
    run_expect(loop, "8.8.4.4", &opt, HDNS_STATUS_OK, 1);

    // 2) numeric IPv6 fast path
    {
        hdns_options_t o6 = opt;
        o6.family = HDNS_QUERY_BOTH;
        run_expect(loop, "::1", &o6, HDNS_STATUS_OK, 1);
    }

    // 3) /etc/hosts: localhost should resolve without touching the mock
    {
        int before = g_mock_queries;
        hdns_options_t oh = opt;
        oh.family = HDNS_QUERY_BOTH;
        run_expect(loop, "localhost", &oh, HDNS_STATUS_OK, 1);
        assert(g_mock_queries == before); // hosts hit, no query sent
    }

    // 4) real query round-trip against the mock nameserver
    {
        int before = g_mock_queries;
        run_expect(loop, "example.test", &opt, HDNS_STATUS_OK, 1);
        assert(g_mock_queries > before); // a question reached the mock
    }

    // 5) cache hit: second resolve of same host must not query the mock again
    {
        int before = g_mock_queries;
        run_expect(loop, "example.test", &opt, HDNS_STATUS_OK, 1);
        assert(g_mock_queries == before); // served from cache
    }

    // 6) NXDOMAIN
    {
        hdns_options_t onx = opt;
        onx.use_cache = 0;
        run_expect(loop, "nx.test", &onx, HDNS_STATUS_NXDOMAIN, 0);
    }

    // 7) cancel before completion: callback must not fire
    {
        g_cancel_cb_called = 0;
        hdns_options_t oc = opt;
        oc.use_cache = 0;
        // use a host the mock does not answer quickly? It answers immediately,
        // so cancel synchronously right after issuing, before the deferred send.
        hdns_t* q = hdns_resolve_ex(loop, "cancel.test", &oc, on_never, NULL);
        assert(q != NULL);
        hdns_cancel(q);
        // spin the loop briefly to ensure no callback is delivered
        htimer_add(loop, stop_after, 200, 1);
        hloop_run(loop);
        assert(g_cancel_cb_called == 0);
        printf("[cancel.test] callback correctly NOT called\n");
    }

    hio_close(mock);
    hloop_free(&loop);
    printf("\nALL hdns_test PASSED\n");
    return 0;
}
