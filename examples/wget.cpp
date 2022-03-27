/*
 * @build: make examples
 * @server bin/httpd -s restart -d
 * @client bin/wget http://127.0.0.1:8080/
 */

#include "http_client.h"
#include "htime.h"
using namespace hv;

typedef std::function<void(size_t received_bytes, size_t total_bytes)> wget_progress_cb;

static int wget(const char* url, const char* filepath, wget_progress_cb progress_cb = NULL) {
    HFile file;
    if (file.open(filepath, "wb") != 0) {
        fprintf(stderr, "Failed to open file %s\n", filepath);
        return -20;
    }
    printf("Save file to %s ...\n", filepath);

    HttpClient cli;
    HttpRequest req;
    req.url = url;
    HttpResponse resp;

    // HEAD
    req.method = HTTP_HEAD;
    int ret = cli.send(&req, &resp);
    if (ret != 0) {
        fprintf(stderr, "request error: %d\n", ret);
        return -1;
    }
    printd("%s", resp.Dump(true, false).c_str());
    if (resp.status_code == HTTP_STATUS_NOT_FOUND) {
        fprintf(stderr, "404 Not Found\n");
        return -1;
    }

    bool use_range = false;
    int range_bytes = 1 << 20; // 1M
    std::string accept_ranges = resp.GetHeader("Accept-Ranges");
    size_t content_length = hv::from_string<size_t>(resp.GetHeader("Content-Length"));
    // use Range if server accept_ranges and content_length > 1M
    if (resp.status_code == 200 &&
        accept_ranges == "bytes" &&
        content_length > range_bytes) {
        use_range = true;
    }

    // GET
    req.method = HTTP_GET;
    req.timeout = 3600; // 1h
    if (!use_range) {
        size_t received_bytes = 0;
        req.http_cb = [&file, &content_length, &received_bytes, &progress_cb]
            (HttpMessage* resp, http_parser_state state, const char* data, size_t size) {
            if (state == HP_HEADERS_COMPLETE) {
                content_length = hv::from_string<size_t>(resp->GetHeader("Content-Length"));
                printd("%s", resp->Dump(true, false).c_str());
            } else if (state == HP_BODY) {
                if (data && size) {
                    file.write(data, size);
                    received_bytes += size;

                    if (progress_cb) {
                        progress_cb(received_bytes, content_length);
                    }
                }
            }
        };
        ret = cli.send(&req, &resp);
        if (ret != 0) {
            fprintf(stderr, "request error: %d\n", ret);
            return -1;
        }
        return 0;
    }

    // Range: bytes=from-to
    long from = 0, to = 0;
    while (from < content_length) {
        to = from + range_bytes - 1;
        if (to >= content_length) to = content_length - 1;
        req.SetRange(from, to);
        printd("%s", req.Dump(true, false).c_str());
        ret = cli.send(&req, &resp);
        if (ret != 0) {
            fprintf(stderr, "request error: %d\n", ret);
            return -1;
        }
        printd("%s", resp.Dump(true, false).c_str());
        file.write(resp.body.data(), resp.body.size());
        from = to + 1;

        if (progress_cb) {
            progress_cb(from, content_length);
        }
    }

    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s url [filepath]\n", argv[0]);
        return -10;
    }
    const char* url = argv[1];
    const char* filepath = "index.html";
    if (argc > 2) {
        filepath = argv[2];
    } else {
        const char* path = strrchr(url, '/');
        if (path && path[1]) {
            filepath = path + 1;
        }
    }

    unsigned int start_time = gettick_ms();
    int last_progress = 0;
    wget(url, filepath, [&last_progress](size_t received_bytes, size_t total_bytes) {
        // print progress
        if (total_bytes == 0) {
            printf("\rprogress: %lu/? = ?", (unsigned long)received_bytes);
        } else {
            int cur_progress = received_bytes * 100 / total_bytes;
            if (cur_progress > last_progress) {
                printf("\rprogress: %lu/%lu = %d%%", (unsigned long)received_bytes, (unsigned long)total_bytes, (int)cur_progress);
                last_progress = cur_progress;
            }
        }
        fflush(stdout);
    });
    unsigned int end_time = gettick_ms();
    printf("\ncost time %u ms\n", end_time - start_time);

    return 0;
}
