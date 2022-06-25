/*
 * @build: make examples
 * @server bin/httpd -s restart -d
 * @usage: bin/curl -v www.baidu.com
 *         bin/curl -v 127.0.0.1:8080
 *         bin/curl -v 127.0.0.1:8080/ping
 *         bin/curl -v 127.0.0.1:8080/echo -d 'hello,world!'
 */

#include "http_client.h"
#include "hurl.h"

#ifdef _MSC_VER
#include "misc/win32_getopt.h"
#else
#include <getopt.h>
#endif

static bool verbose         = false;
static const char* method   = NULL;
static const char* url      = "/";
static int  http_version    = 1;
static int  grpc            = 0;
static int  send_count      = 1;
static int  retry_count     = 0;
static int  retry_delay     = 3;
static int  timeout         = 0;

static int lopt = 0;
static const char* http_proxy   = NULL;
static const char* https_proxy  = NULL;
static const char* no_proxy     = NULL;

static const char* options = "hVvX:H:r:d:F:n:";
static const struct option long_options[] = {
    {"help",    no_argument,        NULL,   'h'},
    {"verion",  no_argument,        NULL,   'V'},
    {"verbose", no_argument,        NULL,   'v'},
    {"method",  required_argument,  NULL,   'X'},
    {"header",  required_argument,  NULL,   'H'},
    {"range",   required_argument,  NULL,   'r'},
    {"data",    required_argument,  NULL,   'd'},
    {"form",    required_argument,  NULL,   'F'},
    {"count",   required_argument,  NULL,   'n'},
    {"http2",   no_argument,        &http_version, 2},
    {"grpc",    no_argument,        &grpc,  1},
    \
    {"http-proxy",  required_argument,  &lopt,  1},
    {"https-proxy", required_argument,  &lopt,  2},
    {"no-proxy",    required_argument,  &lopt,  3},
    {"retry",       required_argument,  &lopt,  4},
    {"delay",       required_argument,  &lopt,  5},
    {"timeout",     required_argument,  &lopt,  6},
    \
    {NULL,      0,                  NULL,   0}
};
static const char* help = R"(Options:
    -h|--help           Print this message.
    -V|--version        Print version.
    -v|--verbose        Show verbose infomation.
    -X|--method         Set http method.
    -H|--header         Add http header, -H "Content-Type: application/json"
    -r|--range          Add http header Range:bytes=0-1023
    -d|--data           Set http body.
    -F|--form           Set http form, -F "name=value" -F "file=@filename"
    -n|--count          Send request count, used for test keep-alive
       --http2          Use http2
       --grpc           Use grpc over http2
       --http-proxy     Set http proxy
       --https-proxy    Set https proxy
       --no-proxy       Set no proxy
       --retry          Set fail retry count
       --timeout        Set timeout, unit(s)

Examples:
    curl -v GET  httpbin.org/get
    curl -v POST httpbin.org/post   user=admin pswd=123456
    curl -v PUT  httpbin.org/put    user=admin pswd=123456
    curl -v localhost:8080
    curl -v localhost:8080 -r 0-9
    curl -v localhost:8080/ping
    curl -v localhost:8080/query?page_no=1\&page_size=10
    curl -v localhost:8080/echo     hello,world!
    curl -v localhost:8080/kv       user=admin\&pswd=123456
    curl -v localhost:8080/json     user=admin pswd=123456
    curl -v localhost:8080/form     -F file=@filename
    curl -v localhost:8080/upload   @filename
)";

static void print_usage() {
    fprintf(stderr, "Usage: curl [%s] [METHOD] url [header_field:header_value] [body_key=body_value]\n", options);
}
static void print_version() {
    fprintf(stderr, "curl version 1.0.0\n");
}
static void print_help() {
    print_usage();
    puts(help);
    print_version();
}

static bool is_upper_string(const char* str) {
    const char* p = str;
    while (*p >= 'A' && *p <= 'Z') ++p;
    return *p == '\0';
}

static int parse_data(char* arg, HttpRequest* req) {
    char* pos = NULL;
    // @filename
    if (arg[0] == '@') {
        req->File(arg + 1);
        return 0;
    }

    // k1=v1&k2=v2
    hv::KeyValue kvs = hv::splitKV(arg, '&', '=');
    if (kvs.size() >= 2) {
        if (req->ContentType() == CONTENT_TYPE_NONE) {
            req->content_type = X_WWW_FORM_URLENCODED;
        }
        for (auto& kv : kvs) {
            req->Set(kv.first.c_str(), kv.second);
        }
        return 0;
    }

    // k=v
    if ((pos = strchr(arg, '=')) != NULL) {
        *pos = '\0';
        if (pos[1] == '@') {
            // file=@filename
            req->content_type = MULTIPART_FORM_DATA;
            req->SetFormFile(optarg, pos + 2);
        } else {
            if (req->ContentType() == CONTENT_TYPE_NONE) {
                req->content_type = APPLICATION_JSON;
            }
            req->Set(arg, pos + 1);
        }
        return 0;
    }

    if (req->ContentType() == CONTENT_TYPE_NONE) {
        req->content_type = TEXT_PLAIN;
    }
    req->body = arg;
    return 0;
}

static int parse_cmdline(int argc, char* argv[], HttpRequest* req) {
    int opt;
    int opt_idx;
    char* pos = NULL;
    while ((opt = getopt_long(argc, argv, options, long_options, &opt_idx)) != EOF) {
        switch(opt) {
        case 'h': print_help();     exit(0);
        case 'V': print_version();  exit(0);
        case 'v': verbose = true;   break;
        case 'X': method = optarg;  break;
        case 'H':
            // -H "Content-Type: application/json"
            pos = strchr(optarg, ':');
            if (pos) {
                *pos = '\0';
                req->headers[optarg] = hv::trim(pos + 1);
                *pos = ':';
            }
            break;
        case 'r':
            req->headers["Range"] = std::string("bytes=").append(optarg);
            break;
        case 'd':
            parse_data(optarg, req);
            break;
        case 'F':
            pos = strchr(optarg, '=');
            if (pos) {
                req->content_type = MULTIPART_FORM_DATA;
                *pos = '\0';
                if (pos[1] == '@') {
                    // -F file=@filename
                    req->SetFormFile(optarg, pos + 2);
                } else {
                    // -F name=value
                    req->SetFormData(optarg, pos + 1);
                }
                *pos = '=';
            }
            break;
        case 'n': send_count = atoi(optarg); break;
        case  0 :
        {
            switch (lopt) {
            case  1: http_proxy  = optarg;      break;
            case  2: https_proxy = optarg;      break;
            case  3: no_proxy    = optarg;      break;
            case  4: retry_count = atoi(optarg);break;
            case  5: retry_delay = atoi(optarg);break;
            case  6: timeout     = atoi(optarg);break;
            default: break;
            }
        }
        default: break;
        }
    }

    if (optind == argc) {
        fprintf(stderr, "Missing url\n");
        print_usage();
        exit(-1);
    }

    if (is_upper_string(argv[optind])) {
        method = argv[optind++];
    }
    url = argv[optind++];

    for (int d = optind; d < argc; ++d) {
        char* arg = argv[d];
        if ((pos = strchr(arg, ':')) != NULL) {
            *pos = '\0';
            req->headers[arg] = pos + 1;
        } else {
            parse_data(arg, req);
        }
    }

    // --http2
    if (http_version == 2) {
        req->http_major = 2;
        req->http_minor = 0;
    }
    // --grpc
    if (grpc) {
        http_version = 2;
        req->content_type = APPLICATION_GRPC;
    }
    // --timeout
    if (timeout > 0) {
        req->timeout = timeout;
    }

    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 0;
    }

    int ret = 0;
    HttpRequest req;
    parse_cmdline(argc, argv, &req);
    if (method) {
        req.method = http_method_enum(method);
    } else {
        req.DumpBody();
        if (req.body.empty()) {
            req.method = HTTP_GET;
        } else {
            req.method = HTTP_POST;
        }
    }
    req.url = HUrl::escapeUrl(url);
    req.http_cb = [](HttpMessage* res, http_parser_state state, const char* data, size_t size) {
        if (state == HP_HEADERS_COMPLETE) {
            if (verbose) {
                fprintf(stderr, "%s", res->Dump(true, false).c_str());
            }
        } else if (state == HP_BODY) {
            if (data && size) {
                printf("%.*s", (int)size, data);
                // This program no need to save data to body.
                // res->body.append(data, size);
            }
        }
    };

    hv::HttpClient cli;
    // http_proxy
    if (http_proxy) {
        hv::StringList ss = hv::split(http_proxy, ':');
        const char* host = ss[0].c_str();
        int port = ss.size() == 2 ? hv::from_string<int>(ss[1]) : DEFAULT_HTTP_PORT;
        fprintf(stderr, "* http_proxy=%s:%d\n", host, port);
        cli.setHttpProxy(host, port);
    }
    // https_proxy
    if (https_proxy) {
        hv::StringList ss = hv::split(https_proxy, ':');
        const char* host = ss[0].c_str();
        int port = ss.size() == 2 ? hv::from_string<int>(ss[1]) : DEFAULT_HTTPS_PORT;
        fprintf(stderr, "* https_proxy=%s:%d\n", host, port);
        cli.setHttpsProxy(host, port);
    }
    // no_proxy
    if (no_proxy) {
        hv::StringList ss = hv::split(no_proxy, ',');
        fprintf(stderr, "* no_proxy=");
        for (const auto& s : ss) {
            fprintf(stderr, "%s,", s.c_str());
            cli.addNoProxy(s.c_str());
        }
        fprintf(stderr, "\n");
    }

send:
    if (verbose) {
        fprintf(stderr, "%s\n", req.Dump(true, true).c_str());
    }
    HttpResponse res;
    ret = cli.send(&req, &res);
    if (ret != 0) {
        fprintf(stderr, "* Failed:%s:%d\n", http_client_strerror(ret), ret);
        if (retry_count > 0) {
            fprintf(stderr, "\nretry again later...%d\n", retry_count);
            --retry_count;
            hv_sleep(retry_delay);
            goto send;
        }
    }
    if (--send_count > 0) {
        fprintf(stderr, "\nsend again later...%d\n", send_count);
        hv_sleep(retry_delay);
        goto send;
    }
    return ret;
}
