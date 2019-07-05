#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _MSC_VER
#include "misc/win32_getopt.h"
#else
#include <getopt.h>
#endif

#include "http_client.h"

static const char* options = "hVvX:H:d:";
static const struct option long_options[] = {
    {"help",    no_argument,        NULL,   'h'},
    {"verion",  no_argument,        NULL,   'V'},
    {"verbose", no_argument,        NULL,   'v'},
    {"method",  required_argument,  NULL,   'X'},
    {"header",  required_argument,  NULL,   'H'},
    {"data",    required_argument,  NULL,   'd'},
    {NULL,      0,                  NULL,   0}
};
static const char* help = R"(Options:
    -h|--help           Print this message.
    -V|--version        Print version.
    -v|--verbose        Show verbose infomation.
    -X|--method         Set http method.
    -H|--header         Add http headers, format -H "Content-Type:application/json Accept:*/*"
    -d|--data           Set http body.
Examples:
    curl -v localhost:8086
    curl -v localhost:8086/v1/api/query?page_no=1&page_size=10
    curl -v -X POST localhost:8086/v1/api/json  -H "Content-Type:application/json"                  -d '{"user":"admin","pswd":"123456"}'
    curl -v -X POST localhost:8086/v1/api/kv    -H "Content-Type:application/x-www-form-urlencoded" -d 'user=admin&pswd=123456'
    curl -v -X POST localhost:8086/v1/api/echo  -H "Content-Type:text/plain"                        -d 'hello,world!'
)";

void print_usage() {
    printf("Usage: curl [%s] url\n", options);
}
void print_version() {
    printf("curl version 1.0.0\n");
}
void print_help() {
    print_usage();
    puts(help);
    print_version();
}

bool verbose = false;
static const char* url = NULL;
static const char* method = NULL;
static const char* headers = NULL;
static const char* data = NULL;
int parse_cmdline(int argc, char* argv[]) {
    int opt;
    int opt_idx;
    while ((opt = getopt_long(argc, argv, options, long_options, &opt_idx)) != EOF) {
        switch(opt) {
        case 'h': print_help(); exit(0);
        case 'V': print_version(); exit(0);
        case 'v': verbose = true; break;
        case 'X': method = optarg; break;
        case 'H': headers = optarg; break;
        case 'd': data = optarg; break;
        default:
            print_usage();
            exit(-1);
        }
    }

    if (optind == argc) {
        printf("Missing url\n");
        print_usage();
        exit(-1);
    }
    url = argv[optind];

    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 0;
    }

    parse_cmdline(argc, argv);

    int ret = 0;
    HttpRequest req;
    req.url = url;
    if (method) {
        req.method = http_method_enum(method);
    }
    if (headers) {
        enum {
            s_key,
            s_value,
        } state = s_key;
        const char* p = headers;
        const char* key = p;
        const char* value = NULL;
        int key_len = 0;
        int value_len = 0;
        while (*p != '\0') {
            if (*p == ' ') {
                if (key_len && value_len) {
                    req.headers[std::string(key,key_len)] = std::string(value,value_len);
                    key_len = value_len = 0;
                }
                state = s_key;
                key = p+1;
            }
            else if (*p == ':') {
                state = s_value;
                value = p+1;
            }
            else {
                state == s_key ? ++key_len : ++value_len;
            }
            ++p;
        }
        if (key_len && value_len) {
            req.headers[std::string(key,key_len)] = std::string(value,value_len);
            key_len = value_len = 0;
        }
    }
    if (data) {
        if (method == NULL) {
            req.method = HTTP_POST;
        }
        req.body = data;
    }
    HttpResponse res;
    ret = http_client_send(&req, &res, 0);
    if (verbose) {
        printf("%s\n", req.dump(true,true).c_str());
    }
    if (ret != 0) {
        printf("* Failed:%s:%d\n", http_client_strerror(ret), ret);
    }
    else {
        if (verbose) {
            printf("%s\n", res.dump(true,true).c_str());
        }
        else {
            printf("%s\n", res.body.c_str());
        }
    }
    return ret;
}
