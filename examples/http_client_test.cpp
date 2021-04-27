#include "requests.h"

#include "hthread.h" // import hv_gettid

static void test_http_async_client(http_client_t* cli, int* finished) {
    printf("test_http_async_client request thread tid=%ld\n", hv_gettid());
    HttpRequestPtr req(new HttpRequest);
    req->method = HTTP_POST;
    req->url = "127.0.0.1:8080/echo";
    req->headers["Connection"] = "keep-alive";
    req->body = "This is an async request.";
    req->timeout = 10;
    http_client_send_async(cli, req, [finished](const HttpResponsePtr& resp) {
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

static void test_http_sync_client(http_client_t* cli) {
    HttpRequest req;
    req.method = HTTP_POST;
    req.url = "127.0.0.1:8080/echo";
    req.headers["Connection"] = "keep-alive";
    req.body = "This is a sync request.";
    req.timeout = 10;
    HttpResponse resp;
    int ret = http_client_send(cli, &req, &resp);
    if (ret != 0) {
        printf("request failed!\n");
    } else {
        printf("%d %s\r\n", resp.status_code, resp.status_message());
        printf("%s\n", resp.body.c_str());
    }
}

static void test_requests() {
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
    jroot["pswd"] = "123456";
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

int main(int argc, char* argv[]) {
    int cnt = 0;
    if (argc > 1) cnt = atoi(argv[1]);
    if (cnt == 0) cnt = 1;

    http_client_t* sync_client = http_client_new();
    http_client_t* async_client = http_client_new();
    int finished = 0;

    for (int i = 0; i < cnt; ++i) {
        test_http_async_client(async_client, &finished);

        test_http_sync_client(sync_client);

        // like python requests
        test_requests();
    }

    http_client_del(sync_client);
    // demo wait async finished
    while (!finished) hv_delay(100);
    printf("finished!\n");
    http_client_del(async_client);

    return 0;
}
