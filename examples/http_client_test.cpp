#include "requests.h"

#include "hthread.h" // import hv_gettid

static void onResponse(int state, HttpRequestPtr req, HttpResponsePtr resp, void* userdata) {
    printf("test_http_async_client response thread tid=%ld\n", hv_gettid());
    if (state != 0) {
        printf("onError: %s:%d\n", http_client_strerror(state), state);
    } else {
        printf("onSuccess\n");
        printf("%d %s\r\n", resp->status_code, resp->status_message());
        printf("%s\n", resp->body.c_str());
    }

    int* finished = (int*)userdata;
    *finished = 1;
}

static void test_http_async_client(int* finished) {
    printf("test_http_async_client request thread tid=%ld\n", hv_gettid());
    HttpRequestPtr req = HttpRequestPtr(new HttpRequest);
    HttpResponsePtr resp = HttpResponsePtr(new HttpResponse);
    req->method = HTTP_POST;
    req->url = "127.0.0.1:8080/echo";
    req->body = "this is an async request.";
    req->timeout = 10;
    int ret = http_client_send_async(req, resp, onResponse, (void*)finished);
    if (ret != 0) {
        printf("http_client_send_async error: %s:%d\n", http_client_strerror(ret), ret);
        *finished = 1;
    }
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

    resp = requests::post("127.0.0.1:8080/echo", "hello,world!");
    if (resp == NULL) {
        printf("request failed!\n");
    } else {
        printf("%d %s\r\n", resp->status_code, resp->status_message());
        printf("%s\n", resp->body.c_str());
    }
}

int main() {
    int finished = 0;
    test_http_async_client(&finished);

    test_http_sync_client();

    // demo wait async ResponseCallback
    while (!finished) {
        hv_delay(100);
    }
    printf("finished!\n");

    return 0;
}
