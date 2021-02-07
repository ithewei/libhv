#include "requests.h"

#include "hthread.h" // import hv_gettid

static void test_http_async_client(int* finished) {
    printf("test_http_async_client request thread tid=%ld\n", hv_gettid());
    HttpRequestPtr req(new HttpRequest);
    req->method = HTTP_POST;
    req->url = "127.0.0.1:8080/echo";
    req->headers["Connection"] = "keep-alive";
    req->body = "this is an async request.";
    req->timeout = 10;
    http_client_send_async(req, [finished](const HttpResponsePtr& resp) {
        printf("test_http_async_client response thread tid=%ld\n", hv_gettid());
        if (resp == NULL) {
            printf("request failed!\n");
        } else {
            printf("%d %s\r\n", resp->status_code, resp->status_message());
            printf("%s\n", resp->body.c_str());
        }
        *finished = 1;
    });
}

static void test_http_sync_client() {
    // auto resp = requests::get("http://www.example.com");
    //
    // make clean && make WITH_OPENSSL=yes
    // auto resp = requests::get("https://www.baidu.com");

    auto resp = requests::get("http://127.0.0.1:8080/ping");
    if (resp == NULL) {
        printf("request failed!\n");
    } else {
        printf("%d %s\r\n", resp->status_code, resp->status_message());
        printf("%s\n", resp->body.c_str());
    }

    hv::Json jroot;
    jroot["user"] = "admin";
    http_headers headers;
    headers["Content-Type"] = "application/json";
    resp = requests::post("127.0.0.1:8080/echo", jroot.dump(), headers);
    if (resp == NULL) {
        printf("request failed!\n");
    } else {
        printf("%d %s\r\n", resp->status_code, resp->status_message());
        printf("%s\n", resp->body.c_str());
    }
}

int main() {
    int finished = 0;

    int cnt = 1;
    for (int i = 0; i < cnt; ++i) {
        test_http_async_client(&finished);

        test_http_sync_client();

        hv_delay(1000);
    }

    // demo wait async ResponseCallback
    while (!finished) {
        hv_delay(100);
    }
    printf("finished!\n");

    return 0;
}
