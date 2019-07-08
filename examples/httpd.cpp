#include "h.h"
#include "hmain.h"
#include "httpd_conf.h"
#include "http_server.h"
#include "http_api_test.h"

httpd_conf_ctx_t g_conf_ctx;
HttpService g_http_service;

static void print_version();
static void print_help();

static int  parse_confile(const char* confile);

// short options
static const char options[] = "hvc:ts:dp:";
// long options
static const option_t long_options[] = {
    {'h', "help",       NO_ARGUMENT},
    {'v', "version",    NO_ARGUMENT},
    {'c', "confile",    REQUIRED_ARGUMENT},
    {'t', "test",       NO_ARGUMENT},
    {'s', "signal",     REQUIRED_ARGUMENT},
    {'d', "daemon",     NO_ARGUMENT},
    {'p', "port",       REQUIRED_ARGUMENT}
};
static const char detail_options[] = R"(
  -h|--help                 Print this information
  -v|--version              Print version
  -c|--confile <confile>    Set configure file, default etc/{program}.conf
  -t|--test                 Test Configure file and exit
  -s|--signal <signal>      Send <signal> to process,
                            <signal>=[start,stop,restart,status,reload]
  -d|--daemon               Daemonize
  -p|--port <port>          Set listen port
)";

void print_version() {
    printf("%s version %s\n", g_main_ctx.program_name, get_compile_version());
}

void print_help() {
    printf("Usage: %s [%s]\n", g_main_ctx.program_name, options);
    printf("Options:\n%s\n", detail_options);
}

int parse_confile(const char* confile) {
    int ret = g_conf_ctx.parser->LoadFromFile(confile);
    if (ret != 0) {
        printf("Load confile [%s] failed: %d\n", confile, ret);
        exit(-40);
    }

    // logfile
    string str = g_conf_ctx.parser->GetValue("logfile");
    if (!str.empty()) {
        strncpy(g_main_ctx.logfile, str.c_str(), sizeof(g_main_ctx.logfile));
    }
    hlog_set_file(g_main_ctx.logfile);
    // loglevel
    const char* szLoglevel = g_conf_ctx.parser->GetValue("loglevel").c_str();
    if (stricmp(szLoglevel, "VERBOSE") == 0) {
        g_conf_ctx.loglevel = LOG_LEVEL_VERBOSE;
    } else if (stricmp(szLoglevel, "DEBUG") == 0) {
        g_conf_ctx.loglevel = LOG_LEVEL_DEBUG;
    } else if (stricmp(szLoglevel, "INFO") == 0) {
        g_conf_ctx.loglevel = LOG_LEVEL_INFO;
    } else if (stricmp(szLoglevel, "WARN") == 0) {
        g_conf_ctx.loglevel = LOG_LEVEL_WARN;
    } else if (stricmp(szLoglevel, "ERROR") == 0) {
        g_conf_ctx.loglevel = LOG_LEVEL_ERROR;
    } else if (stricmp(szLoglevel, "FATAL") == 0) {
        g_conf_ctx.loglevel = LOG_LEVEL_FATAL;
    } else if (stricmp(szLoglevel, "SILENT") == 0) {
        g_conf_ctx.loglevel = LOG_LEVEL_SILENT;
    } else {
        g_conf_ctx.loglevel = LOG_LEVEL_VERBOSE;
    }
    hlog_set_level(g_conf_ctx.loglevel);
    // log_remain_days
    str = g_conf_ctx.parser->GetValue("log_remain_days");
    if (!str.empty()) {
        hlog_set_remain_days(atoi(str.c_str()));
    }
    hlogi("%s version: %s", g_main_ctx.program_name, get_compile_version());

    // worker_processes
    int worker_processes = 0;
    str = g_conf_ctx.parser->GetValue("worker_processes");
    if (str.size() != 0) {
        if (strcmp(str.c_str(), "auto") == 0) {
            worker_processes = get_ncpu();
            hlogd("worker_processes=ncpu=%d", worker_processes);
        }
        else {
            worker_processes = atoi(str.c_str());
        }
    }
    g_conf_ctx.worker_processes = LIMIT(0, worker_processes, MAXNUM_WORKER_PROCESSES);

    // port
    int port = 0;
    const char* szPort = get_arg("p");
    if (szPort) {
        port = atoi(szPort);
    }
    if (port == 0) {
        port = atoi(g_conf_ctx.parser->GetValue("port").c_str());
    }
    if (port == 0) {
        printf("Please config listen port!\n");
        exit(-10);
    }
    g_conf_ctx.port = port;

    // http server
    // base_url
    str = g_conf_ctx.parser->GetValue("base_url");
    if (str.size() != 0) {
        g_http_service.base_url = str;
    }
    // document_root
    str = g_conf_ctx.parser->GetValue("document_root");
    if (str.size() != 0) {
        g_http_service.document_root = str;
    }
    // home_page
    str = g_conf_ctx.parser->GetValue("home_page");
    if (str.size() != 0) {
        g_http_service.home_page = str;
    }
    // error_page
    str = g_conf_ctx.parser->GetValue("error_page");
    if (str.size() != 0) {
        g_http_service.error_page = str;
    }

    return 0;
}

static void on_reload(void* userdata) {
    hlogi("reload confile [%s]", g_main_ctx.confile);
    parse_confile(g_main_ctx.confile);
}

int main(int argc, char** argv) {
    // g_main_ctx
    main_ctx_init(argc, argv);
    //int ret = parse_opt(argc, argv, options);
    int ret = parse_opt_long(argc, argv, long_options, ARRAY_SIZE(long_options));
    if (ret != 0) {
        print_help();
        exit(ret);
    }

    /*
    printf("---------------arg------------------------------\n");
    printf("%s\n", g_main_ctx.cmdline);
    for (auto& pair : g_main_ctx.arg_kv) {
        printf("%s=%s\n", pair.first.c_str(), pair.second.c_str());
    }
    for (auto& item : g_main_ctx.arg_list) {
        printf("%s\n", item.c_str());
    }
    printf("================================================\n");
    */

    /*
    printf("---------------env------------------------------\n");
    for (auto& pair : g_main_ctx.env_kv) {
        printf("%s=%s\n", pair.first.c_str(), pair.second.c_str());
    }
    printf("================================================\n");
    */

    // help
    if (get_arg("h")) {
        print_help();
        exit(0);
    }

    // version
    if (get_arg("v")) {
        print_version();
        exit(0);
    }

    // g_conf_ctx
    conf_ctx_init(&g_conf_ctx);
    const char* confile = get_arg("c");
    if (confile) {
        strncpy(g_main_ctx.confile, confile, sizeof(g_main_ctx.confile));
    }
    parse_confile(g_main_ctx.confile);

    // test
    if (get_arg("t")) {
        printf("Test confile [%s] OK!\n", g_main_ctx.confile);
        exit(0);
    }

    // signal
    signal_init(on_reload);
    const char* signal = get_arg("s");
    if (signal) {
        handle_signal(signal);
    }

#ifdef OS_UNIX
    // daemon
    if (get_arg("d")) {
        // nochdir, noclose
        int ret = daemon(1, 1);
        if (ret != 0) {
            printf("daemon error: %d\n", ret);
            exit(-10);
        }
        // parent process exit after daemon, so pid changed.
        g_main_ctx.pid = getpid();
    }
#endif

    // pidfile
    create_pidfile();

    // HttpService
    g_http_service.preprocessor = http_api_preprocessor;
    g_http_service.postprocessor = http_api_postprocessor;
#define XXX(path, method, handler) \
    g_http_service.AddApi(path, HTTP_##method, handler);
    HTTP_API_MAP(XXX)
#undef XXX

    // http_server
    http_server_t srv;
    srv.port = g_conf_ctx.port;
    srv.worker_processes = g_conf_ctx.worker_processes;
    srv.service = &g_http_service;
    ret = http_server_run(&srv);
    return ret;
}
