/*
 * @build   make examples
 *
 * @server  bin/http_server_test 8080
 *
 * @client  bin/http_client_test
 *
 */

#include "requests.h"
#include "axios.h"
using namespace hv;

#include "hthread.h" // import hv_gettid

static void test_http_async_client(HttpClient* cli, int* resp_cnt) {
    printf("test_http_async_client request thread tid=%ld\n", hv_gettid());
    HttpRequestPtr req(new HttpRequest);
    req->method = HTTP_POST;
    req->url = "http://127.0.0.1:8080/echo";
    req->headers["Connection"] = "keep-alive";
    req->body = "This is an async request.";
    req->timeout = 10;
    cli->sendAsync(req, [resp_cnt](const HttpResponsePtr& resp) {
        printf("test_http_async_client response thread tid=%ld\n", hv_gettid());
        if (resp == NULL) {
            printf("request failed!\n");
        } else {
            printf("%d %s\r\n", resp->status_code, resp->status_message());
            printf("%s\n", resp->body.c_str());
        }
        *resp_cnt += 1;
    });
}

static void test_http_sync_client(HttpClient* cli) {
    HttpRequest req;
    req.method = HTTP_POST;
    req.url = "http://127.0.0.1:8080/echo";
    req.headers["Connection"] = "keep-alive";
    req.body = "This is a sync request.";
    req.timeout = 10;
    HttpResponse resp;
    int ret = cli->send(&req, &resp);
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

    // Content-Type: application/json
    hv::Json jroot;
    jroot["user"] = "admin";
    jroot["pswd"] = "123456";
    http_headers headers;
    headers["Content-Type"] = "application/json";
    resp = requests::post("http://127.0.0.1:8080/echo", jroot.dump(), headers);
    if (resp == NULL) {
        printf("request failed!\n");
    } else {
        printf("%d %s\r\n", resp->status_code, resp->status_message());
        printf("%s\n", resp->body.c_str());
    }

    // Content-Type: multipart/form-data
    std::map<std::string, std::string> params;
    params["user"] = "admin";
    params["pswd"] = "123456";
    resp = requests::uploadFormFile("http://127.0.0.1:8080/echo", "avatar", "avatar.jpg", params);
    if (resp == NULL) {
        printf("uploadFormFile failed!\n");
    } else {
        printf("%d %s\r\n", resp->status_code, resp->status_message());
        printf("%s\n", resp->body.c_str());
    }

    /*
    size_t filesize = requests::downloadFile("http://www.example.com/index.html", "index.html");
    if (filesize == 0) {
        printf("downloadFile failed!\n");
    } else {
        printf("downloadFile success!\n");
    }
    */

    // async
    /*
    // Request req(new HttpRequest);
    req->url = "http://127.0.0.1:8080/echo";
    req->method = HTTP_POST;
    req->body = "This is an async request.";
    req->timeout = 10;
    requests::async(req, [](const HttpResponsePtr& resp) {
        if (resp == NULL) {
            printf("request failed!\n");
        } else {
            printf("%d %s\r\n", resp->status_code, resp->status_message());
            printf("%s\n", resp->body.c_str());
        }
    });
    */
}

static void test_axios() {
    const char* strReq = R"(
    {
        "method": "POST",
        "url": "http://127.0.0.1:8080/echo",
        "timeout": 10,
        "params": {
            "page_no": "1",
            "page_size": "10"
        },
        "headers": {
            "Content-Type": "application/json"
        },
        "body": {
            "app_id": "123456",
            "app_secret": "abcdefg"
        }
    }
    )";

    // sync
    auto resp = axios::axios(strReq);
    // auto resp = axios::post("http://127.0.0.1:8080/echo", strReq);
    if (resp == NULL) {
        printf("request failed!\n");
    } else {
        printf("%d %s\r\n", resp->status_code, resp->status_message());
        printf("%s\n", resp->body.c_str());
    }

    // async
    /*
    axios::axios(strReq, [](const HttpResponsePtr& resp) {
        if (resp == NULL) {
            printf("request failed!\n");
        } else {
            printf("%d %s\r\n", resp->status_code, resp->status_message());
            printf("%s\n", resp->body.c_str());
        }
    });
    */
}

int main(int argc, char* argv[]) {
    int req_cnt = 0;
    if (argc > 1) req_cnt = atoi(argv[1]);
    if (req_cnt == 0) req_cnt = 1;

    HttpClient sync_client;
    HttpClient async_client;
    int resp_cnt = 0;

    for (int i = 0; i < req_cnt; ++i) {
        test_http_async_client(&async_client, &resp_cnt);

        test_http_sync_client(&sync_client);

        test_requests();

        test_axios();
    }

    // demo wait async finished
    while (resp_cnt < req_cnt) hv_delay(100);
    printf("finished!\n");

    return 0;
}
