// 验证 http_client_set_local_addr 能把出包的源地址固定住
//
// 判定依据：绑定 127.0.0.1 之后去连本机的非 loopback 地址，服务端看到的
// 源地址仍然是 127.0.0.1（不绑定时会是本机主地址），说明绑定确实生效。
//
// 手动运行：
//   make unittest && bin/http_client_local_addr_test <本机非loopback地址>
//
// 注意：需要占用一个 TCP 端口，且第二个用例要求传入本机的非 loopback 地址，
// 因此不加入 scripts/unittest.sh 的自动运行列表。

#include "http/client/HttpClient.h"
#include "http/server/HttpServer.h"
#include "http/httpdef.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <unistd.h>

static int g_port = 18099;

static void* server_thread(void* userdata) {
    HttpService router;
    router.GET("/whoami", [](HttpRequest* req, HttpResponse* resp) {
        resp->body = req->client_addr.ip;
        return 200;
    });
    http_server_t server;
    server.port = g_port;
    server.service = &router;
    http_server_run(&server);
    return NULL;
}

// 发起一次请求，返回服务端看到的客户端地址；失败返回空串
static std::string request_client_addr(const char* bind_ip, const char* connect_ip) {
    http_client_t* cli = http_client_new(connect_ip, g_port, 0);
    if (bind_ip) {
        if (http_client_set_local_addr(cli, bind_ip, 0) != 0) {
            http_client_del(cli);
            return "";
        }
    }

    HttpRequest req;
    req.method = HTTP_GET;
    req.url = std::string("http://") + connect_ip + ":" + std::to_string(g_port) + "/whoami";
    HttpResponse resp;
    int ret = http_client_send(cli, &req, &resp);
    std::string addr;
    if (ret == 0) {
        addr = resp.body;
    }
    http_client_del(cli);
    return addr;
}

int main(int argc, char** argv) {
    // 本机主地址，用于验证"绑定生效"这个语义
    const char* host_ip = argc > 1 ? argv[1] : "127.0.0.1";

    // 第二个用例要靠"源地址被绑成 loopback、目标却是主地址"来判定，
    // 如果本机主地址本身就是 loopback，这个用例没有意义
    if (strcmp(host_ip, "127.0.0.1") == 0) {
        printf("SKIP: pass a non-loopback local address as argv[1]\n");
        return 0;
    }

    pthread_t th;
    pthread_create(&th, NULL, server_thread, NULL);
    usleep(1000000);

    // 1) 不绑定，连接 loopback：源地址应为 127.0.0.1
    std::string addr = request_client_addr(NULL, "127.0.0.1");
    printf("no-bind -> 127.0.0.1 : client_addr=[%s]\n", addr.c_str());
    if (addr != "127.0.0.1") {
        printf("FAILED: expected 127.0.0.1\n");
        return 1;
    }

    // 2) 绑定 loopback，连接本机主地址：源地址仍应是 127.0.0.1
    addr = request_client_addr("127.0.0.1", host_ip);
    printf("bind 127.0.0.1 -> %s : client_addr=[%s]\n", host_ip, addr.c_str());
    if (addr != "127.0.0.1") {
        printf("FAILED: the local address is not applied\n");
        return 1;
    }

    // 3) 绑定一个本机不存在的地址：应当失败
    addr = request_client_addr("10.255.255.253", host_ip);
    printf("bind 10.255.255.253 -> %s : client_addr=[%s] (expected to fail)\n", host_ip, addr.c_str());
    if (!addr.empty()) {
        printf("FAILED: binding an unavailable address should fail\n");
        return 1;
    }

    printf("PASS\n");
    return 0;
}
