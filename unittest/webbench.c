#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h> // for gethostbyname

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <signal.h>

#include <getopt.h>
#include <unistd.h>

int Connect(const char* host, int port) {
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    in_addr_t inaddr = inet_addr(host);
    if (inaddr != INADDR_NONE) {
        addr.sin_addr.s_addr = inaddr;
    }
    else {
        struct hostent* phe = gethostbyname(host);
        if (phe == NULL) {
            return -1;
        }
        memcpy(&addr.sin_addr, phe->h_addr_list[0], phe->h_length);
    }
    addr.sin_port = htons(port);
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return -2;
    }
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) <  0) {
        perror("connect");
        return -3;
    }
    return sock;
}

#define METHOD_GET      0
#define METHOD_HEAD     1
#define METHOD_OPTIONS  2
#define METHOD_TRACE    3

#define VERSION         "webbench/1.19.3.15"

int verbose = 0;
int quiet = 0;
volatile int timerexpired = 0; // for timer
int time    = 30;
int clients = 1;
char host[64] = {0};
int  port   = 80;
char* proxy_host = NULL;
int proxy_port = 80;
int method  = METHOD_GET;
int http    = 1; // 1=HTTP/1.1 0=HTTP/1.0
int keepalive = 0;
const char* url = NULL;

#define REQUEST_SIZE    2048
char request[REQUEST_SIZE] = {0};
char buf[1460] = {0};

int mypipe[2]; // IPC

static const char options[] = "?hVvq01kt:p:c:";

static const struct option long_options[] = {
    {"help", no_argument, NULL, 'h'},
    {"version", no_argument, NULL, 'V'},
    {"verbose", no_argument, NULL, 'v'},
    {"quiet", no_argument, NULL, 'q'},
    {"time", required_argument, NULL, 't'},
    {"proxy", required_argument, NULL, 'p'},
    {"clients", required_argument, NULL, 'c'},
    {"http10", no_argument, NULL, '0'},
    {"http11", no_argument, NULL, '1'},
    {"keepalive", no_argument, NULL, 'k'},
    {"get", no_argument, &method, METHOD_GET},
    {"head", no_argument, &method, METHOD_HEAD},
    {"options", no_argument, &method, METHOD_OPTIONS},
    {"trace", no_argument, &method, METHOD_TRACE},
    {NULL, 0, NULL, 0}
};

void print_usage() {
    printf("Usage: webbench [%s] URL\n", options);
    puts("\n\
Options:\n\
  -?|-h|--help              Print this information.\n\
  -V|--version              Print version.\n\
  -v|--verbose              Print verbose.\n\
  -q|--quiet                Print quiet.\n\
  -0|--http10               Use HTTP/1.0 protocol.\n\
  -1|--http11               Use HTTP/1.1 protocol.\n\
  -k|--keepalive            Connection: keep-alive.\n\
  -t|--time <sec>           Run benchmark for <sec> seconds. Default 30.\n\
  -p|--proxy <server:port>  Use proxy server for request.\n\
  -c|--clients <n>          Run <n> HTTP clients. Default one.\n\
  --get                     Use GET request method.\n\
  --head                    Use HEAD request method.\n\
  --options                 Use OPTIONS request method.\n\
  --trace                   Use TRACE request method.\n\
    ");
}

int parse_cmdline(int argc, char** argv) {
    int opt = 0;
    int opt_idx = 0;
    while ((opt=getopt_long(argc, argv, options, long_options, &opt_idx)) != EOF) {
        switch (opt) {
        case '?':
        case 'h': print_usage(); exit(1);
        case 'V': puts(VERSION); exit(1);
        case 'v': verbose = 1; break;
        case 'q': quiet = 1; break;
        case '0': http = 0; break;
        case '1': http = 1; break;
        case 'k': keepalive = 1; break;
        case 't': time = atoi(optarg); break;
        case 'c': clients = atoi(optarg); break;
        case 'p':
            {
                // host:port
                char* pos = strrchr(optarg, ':');
                proxy_host = optarg;
                if (pos == NULL) break;
                if (pos == optarg ||
                    pos == optarg + strlen(optarg) - 1) {
                    printf("Error option --proxy\n");
                    return -2;
                }
                *pos = '\0';
                proxy_port = atoi(pos+1);
            }
            break;
        }
    }

    if (optind == argc) {
        printf("Missing URL\n");
        return -2;
    }

    url = argv[optind];

    return 0;
}

static void alarm_handler(int singal) {
    timerexpired = 1;
}

int main(int argc, char** argv) {
    if (argc == 1) {
        print_usage();
        return 2;
    }

    int ret = parse_cmdline(argc, argv);
    if (ret != 0) {
        return ret;
    }

    printf("%d clients, running %d sec\n", clients, time);

    // domain port url
    const char* req_url = "/";
    if (proxy_host) {
        strncpy(host, proxy_host, sizeof(host));
        port = proxy_port;
    } else {
        // http://host:port/path
        const char* pos1 = strstr(url, "http://");
        if (pos1 == NULL) {
            pos1 = url;
        } else {
            pos1 += strlen("http://");
        }
        const char* pos2 = strchr(pos1, '/');
        if (pos2 == NULL) {
            pos2 = url + strlen(url);
        } else {
            req_url = pos2;
        }
        int len = pos2 - pos1;
        char* server = (char*)malloc(len+1);
        memcpy(server, pos1, len);
        server[len] = '\0';
        char* pos3 = strrchr(server, ':');
        if (pos3 == NULL) {
            port = 80;
        } else {
            *pos3 = '\0';
            port = atoi(pos3+1);
        }
        strncpy(host, server, sizeof(host));
        free(server);
    }
    char Host[256];
    snprintf(Host, sizeof(Host), "Host: %s:%d\r\n", host, port);
    printf("%s", Host);

    // test connect
    int sock = Connect(host, port);
    if (sock < 0) {
        printf("Connect failed!\n");
        return -20;
    } else {
        printf("Connect test OK!\n");
        close(sock);
    }

    // build request
    switch (method) {
    case METHOD_GET:
    default:
        strcpy(request, "GET");
        break;
    case METHOD_HEAD:
        strcpy(request, "HEAD");
        break;
    case METHOD_OPTIONS:
        strcpy(request, "OPTIONS");
        break;
    case METHOD_TRACE:
        strcpy(request, "TRACE");
        break;
    }

    strcat(request, " ");
    strcat(request, req_url);
    strcat(request, " ");
    if (http == 0) {
        strcat(request, "HTTP/1.0");
    } else if (http == 1) {
        strcat(request, "HTTP/1.1");
    }
    strcat(request, "\r\n");
    strcat(request, "User-Agent: webbench/1.18.3.15\r\n");
    strcat(request, Host);
    strcat(request, "Cache-Control: no-cache\r\n");
    if (keepalive) {
        strcat(request, "Connection: keep-alive\r\n");
    }
    else {
        strcat(request, "Connection: close\r\n");
    }
    strcat(request, "\r\n");
    if (!quiet) {
        printf("%s", request);
    }

    // IPC
    if (pipe(mypipe) < 0) {
        perror("pipe");
        exit(20);
    }

    // fork childs
    pid_t pid = 0;
    FILE* fp = NULL;
    long long succeed = 0, failed = 0, bytes = 0;
    int childs = clients;
    int i;
    for (i = 0; i < childs; ++i) {
        pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(-10);
        }

        if (pid == 0) {
            // child
            //printf("child[%d] start\n", getpid());
            signal(SIGALRM, alarm_handler);
            alarm(time);
            int sock = -1;
            int len = strlen(request);
            int wrbytes, rdbytes;
            while (1) {
connect:
                if (timerexpired) break;
                if (sock == -1) {
                    sock = Connect(host, port);
                }
                if (sock < 0) {
                    ++failed;
                    continue;
                }
                int total_rdbytes = 0;
write:
                if (timerexpired) break;
                wrbytes = write(sock, request, len);
                if (verbose) {
                    printf("write %d bytes\n", wrbytes);
                }
                if (wrbytes != len) {
                    ++failed;
                    goto close;
                }
                if (verbose) {
                    printf("%s\n", request);
                }
read:
                if (timerexpired) break;
                rdbytes = read(sock, buf, sizeof(buf));
                if (verbose) {
                    printf("read %d bytes\n", rdbytes);
                }
                if (rdbytes <= 0) {
                    ++failed;
                    goto close;
                }
                if (verbose) {
                    printf("%.*s\n", rdbytes, buf);
                }
                static int s_rdbytes = 0;
                if (s_rdbytes == 0) {
                    s_rdbytes = rdbytes;
                }
                bytes += rdbytes;
                total_rdbytes += rdbytes;
                if (total_rdbytes < s_rdbytes) {
                    // NOTE: some http server head and body send not one packet.
                    goto read;
                }
                ++succeed;
close:
                if (!keepalive) {
                    close(sock);
                    sock = -1;
                }
            }

            fp = fdopen(mypipe[1], "w");
            if (fp == NULL) {
                perror("fdopen");
                return 30;
            }
            fprintf(fp, "%lld %lld %lld\n", succeed, failed, bytes);
            fclose(fp);
            //printf("child[%d] end\n", getpid());
            return 0;
        }
    }

    fp = fdopen(mypipe[0], "r");
    if (fp == NULL) {
        perror("fdopen");
        return 30;
    }
    while (1) {
        long long i,j,k;
        fscanf(fp, "%lld %lld %lld", &i, &j, &k);
        succeed += i;
        failed += j;
        bytes += k;
        if (--childs==0) break;
    }
    fclose(fp);
    printf("recv %lld bytes/sec, %lld succeed, %lld failed\n",
            bytes/time,
            succeed,
            failed);

    return 0;
}
