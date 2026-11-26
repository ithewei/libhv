/*
 * http_static_not_modified_test: If-None-Match and If-Modified-Since handled
 * by the default static file handler (RFC 9110 section 13).
 */

#include <assert.h>
#include <stdio.h>

#include "hbase.h"
#include "hfile.h"
#include "htime.h"
#include "HttpServer.h"
#include "HttpService.h"
#include "requests.h"

static const char* url = "http://127.0.0.1:18083/a.txt";

static int get(const char* name, const std::string& value, const char* name2 = NULL, const std::string& value2 = "") {
    auto req = std::make_shared<HttpRequest>();
    req->url = url;
    req->headers[name] = value;
    if (name2) req->headers[name2] = value2;
    auto resp = requests::request(req);
    assert(resp != NULL);
    printf("%s: %s %s%s%s => %d\n", name, value.c_str(), name2 ? name2 : "", name2 ? ": " : "", value2.c_str(), (int)resp->status_code);
    return resp->status_code;
}

int main() {
    hv_mkdir_p("tmp/http_static_not_modified_test");
    HFile file;
    int ret = file.open("tmp/http_static_not_modified_test/a.txt", "wb");
    assert(ret == 0);
    file.write("0123456789", 10);
    file.close();

    HttpService service;
    service.document_root = "tmp/http_static_not_modified_test";

    hv::HttpServer server(&service);
    server.setPort(18083);
    server.start();
    hv_msleep(200);

    auto resp = requests::get(url);
    assert(resp != NULL && resp->status_code == 200);
    std::string etag = resp->GetHeader("Etag");
    std::string last_modified = resp->GetHeader("Last-Modified");
    assert(!etag.empty() && !last_modified.empty());

    assert(get("If-None-Match", etag) == 304);
    assert(get("If-None-Match", "\"other\", " + etag) == 304);
    assert(get("If-None-Match", "*") == 304);
    assert(get("If-None-Match", "W/" + etag) == 304);
    assert(get("If-None-Match", "\"other\"") == 200);
    assert(get("If-Modified-Since", last_modified) == 304);
    // If-Modified-Since is ignored when If-None-Match is present
    assert(get("If-None-Match", "\"other\"", "If-Modified-Since", last_modified) == 200);

    server.stop();
    printf("http_static_not_modified_test passed\n");
    return 0;
}
