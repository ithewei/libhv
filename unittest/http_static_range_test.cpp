/*
 * http_static_range_test: Range requests served by the default static file
 * handler (RFC 9110 section 14).
 */

#include <assert.h>
#include <stdio.h>

#include "hbase.h"
#include "hfile.h"
#include "htime.h"
#include "HttpServer.h"
#include "HttpService.h"
#include "requests.h"

static void check(const char* range, int status, const char* content_range, const char* body) {
    auto req = std::make_shared<HttpRequest>();
    req->url = "http://127.0.0.1:18082/a.txt";
    req->headers["Range"] = range;
    auto resp = requests::request(req);
    assert(resp != NULL);
    printf("Range: %s => %d %s\n", range, (int)resp->status_code, resp->GetHeader("Content-Range").c_str());
    assert(resp->status_code == status);
    assert(resp->GetHeader("Content-Range") == content_range);
    if (body) assert(resp->body == body);
}

int main() {
    hv_mkdir_p("tmp/http_static_range_test");
    HFile file;
    int ret = file.open("tmp/http_static_range_test/a.txt", "wb");
    assert(ret == 0);
    file.write("0123456789", 10);
    file.close();

    HttpService service;
    service.document_root = "tmp/http_static_range_test";

    hv::HttpServer server(&service);
    server.setPort(18082);
    server.start();
    hv_msleep(200);

    check("bytes=0-0", 206, "bytes 0-0/10", "0");
    check("bytes=5-", 206, "bytes 5-9/10", "56789");
    check("bytes=-3", 206, "bytes 7-9/10", "789");
    check("bytes=-20", 206, "bytes 0-9/10", "0123456789");
    check("bytes=10-", 416, "bytes */10", NULL);
    check("items=0-1", 200, "", "0123456789");

    server.stop();
    printf("http_static_range_test passed\n");
    return 0;
}
