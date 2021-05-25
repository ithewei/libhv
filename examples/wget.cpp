/*
 * @build: make examples
 * @server bin/httpd -s restart -d
 * @client bin/wget 127.0.0.1:8080/
 */

#include "requests.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s url\n", argv[0]);
        return -10;
    }
    const char* url = argv[1];

    std::string filepath;
    const char* path = strrchr(url, '/');
    if (path == NULL || path[1] == '\0') {
        filepath = "index.html";
    } else {
        filepath = path + 1;
    }
    printf("Save file to %s ...\n", filepath.c_str());

    HFile file;
    if (file.open(filepath.c_str(), "wb") != 0) {
        fprintf(stderr, "Failed to open file %s\n", filepath.c_str());
        return -20;
    }

    // HEAD
    requests::Request req(new HttpRequest);
    req->url = url;
    req->method = HTTP_HEAD;
    printd("%s", req->Dump(true, true).c_str());
    auto resp = requests::request(req);
    if (resp == NULL) {
        fprintf(stderr, "request failed!\n");
        return -1;
    }
    printd("%s", resp->Dump(true, false).c_str());

    bool use_range = false;
    int range_bytes = 1 << 20; // 1M
    std::string accept_ranges = resp->GetHeader("Accept-Ranges");
    size_t content_length = hv::from_string<size_t>(resp->GetHeader("Content-Length"));
    // use Range if server accept_ranges and content_length > 1M
    if (resp->status_code == 200 &&
        accept_ranges == "bytes" &&
        content_length > range_bytes) {
        use_range = true;
    }

    // GET
    req->method = HTTP_GET;
    if (!use_range) {
        printd("%s", req->Dump(true, true).c_str());
        resp = requests::get(url);
        if (resp == NULL) {
            fprintf(stderr, "request failed!\n");
            return -1;
        }
        printd("%s", resp->Dump(true, false).c_str());
        file.write(resp->body.data(), resp->body.size());
        return 0;
    }

    // [from, to]
    long from, to;
    from = 0;
    http_client_t* cli = http_client_new();
    while (from < content_length) {
        to = from + range_bytes - 1;
        if (to >= content_length) to = content_length - 1;
        // Range: bytes=from-to
        req->SetRange(from, to);
        printd("%s", req->Dump(true, true).c_str());
        int ret = http_client_send(cli, req.get(), resp.get());
        if (ret != 0) {
            fprintf(stderr, "request failed!\n");
            return -1;
        }
        printd("%s", resp->Dump(true, false).c_str());
        file.write(resp->body.data(), resp->body.size());
        from = to + 1;
    }
    http_client_del(cli);

    return 0;
}
