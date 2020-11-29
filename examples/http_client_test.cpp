#include "requests.h"

int main() {
    auto resp = requests::get("http://www.example.com");
    if (resp == NULL) {
        printf("request failed!\n");
    } else {
        printf("%d %s\r\n", resp->status_code, resp->status_message());
        printf("%s\n", resp->body.c_str());
    }

    auto resp2 = requests::post("127.0.0.1:8080/echo", "hello,world!");
    if (resp2 == NULL) {
        printf("request failed!\n");
    } else {
        printf("%d %s\r\n", resp2->status_code, resp2->status_message());
        printf("%s\n", resp2->body.c_str());
    }

    return 0;
}
