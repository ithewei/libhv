#include "hv.h"
#include "hssl.h"
#include "hmain.h"
#include "iniparser.h"

#include "HttpServer.h"
#include "hasync.h"     // import hv::async

#include "router.h"

hv::HttpServer  g_http_server;
hv::HttpService g_http_service;

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
  -t|--test                 Test configure file and exit
  -s|--signal <signal>      Send <signal> to process,
                            <signal>=[start,stop,restart,status,reload]
  -d|--daemon               Daemonize
  -p|--port <port>          Set listen port
)";

void print_version() {
    printf("%s version %s\n", g_main_ctx.program_name, hv_compile_version());
}

void print_help() {
    printf("Usage: %s [%s]\n", g_main_ctx.program_name, options);
    printf("Options:\n%s\n", detail_options);
}

int parse_confile(const char* confile) {
    IniParser ini;
    int ret = ini.LoadFromFile(confile);
    if (ret != 0) {
        printf("Load confile [%s] failed: %d\n", confile, ret);
        exit(-40);
    }

    // logfile
    std::string str = ini.GetValue("logfile");
    if (!str.empty()) {
        strncpy(g_main_ctx.logfile, str.c_str(), sizeof(g_main_ctx.logfile));
    }
    hlog_set_file(g_main_ctx.logfile);
    // loglevel
    str = ini.GetValue("loglevel");
    if (!str.empty()) {
        hlog_set_level_by_str(str.c_str());
    }
    // log_filesize
    str = ini.GetValue("log_filesize");
    if (!str.empty()) {
        hlog_set_max_filesize_by_str(str.c_str());
    }
    // log_remain_days
    str = ini.GetValue("log_remain_days");
    if (!str.empty()) {
        hlog_set_remain_days(atoi(str.c_str()));
    }
    // log_fsync
    str = ini.GetValue("log_fsync");
    if (!str.empty()) {
        logger_enable_fsync(hlog, hv_getboolean(str.c_str()));
    }
    hlogi("%s version: %s", g_main_ctx.program_name, hv_compile_version());
    hlog_fsync();

    // worker_processes
    int worker_processes = 0;
#ifdef DEBUG
    // Disable multi-processes mode for debugging
    worker_processes = 0;
#else
    str = ini.GetValue("worker_processes");
    if (str.size() != 0) {
        if (strcmp(str.c_str(), "auto") == 0) {
            worker_processes = get_ncpu();
            hlogd("worker_processes=ncpu=%d", worker_processes);
        }
        else {
            worker_processes = atoi(str.c_str());
        }
    }
#endif
    g_http_server.worker_processes = LIMIT(0, worker_processes, MAXNUM_WORKER_PROCESSES);
    // worker_threads
    int worker_threads = 0;
    str = ini.GetValue("worker_threads");
    if (str.size() != 0) {
        if (strcmp(str.c_str(), "auto") == 0) {
            worker_threads = get_ncpu();
            hlogd("worker_threads=ncpu=%d", worker_threads);
        }
        else {
            worker_threads = atoi(str.c_str());
        }
    }
    g_http_server.worker_threads = LIMIT(0, worker_threads, 64);

    // worker_connections
    str = ini.GetValue("worker_connections");
    if (str.size() != 0) {
        g_http_server.worker_connections = atoi(str.c_str());
    }

    // http_port
    int port = 0;
    const char* szPort = get_arg("p");
    if (szPort) {
        port = atoi(szPort);
    }
    if (port == 0) {
        port = ini.Get<int>("port");
    }
    if (port == 0) {
        port = ini.Get<int>("http_port");
    }
    g_http_server.port = port;
    // https_port
    if (HV_WITH_SSL) {
        g_http_server.https_port = ini.Get<int>("https_port");
    }
    if (g_http_server.port == 0 && g_http_server.https_port == 0) {
        printf("Please config listen port!\n");
        exit(-10);
    }

    // base_url
    str = ini.GetValue("base_url");
    if (str.size() != 0) {
        g_http_service.base_url = str;
    }
    // document_root
    str = ini.GetValue("document_root");
    if (str.size() != 0) {
        g_http_service.document_root = str;
    }
    // home_page
    str = ini.GetValue("home_page");
    if (str.size() != 0) {
        g_http_service.home_page = str;
    }
    // error_page
    str = ini.GetValue("error_page");
    if (str.size() != 0) {
        g_http_service.error_page = str;
    }
    // index_of
    str = ini.GetValue("index_of");
    if (str.size() != 0) {
        g_http_service.index_of = str;
    }
    // limit_rate
    str = ini.GetValue("limit_rate");
    if (str.size() != 0) {
        g_http_service.limit_rate = atoi(str.c_str());
    }
    // cors
    if (ini.Get<bool>("cors")) {
        g_http_service.AllowCORS();
    }
    if (ini.Get<bool>("forward_proxy")) {
        g_http_service.EnableForwardProxy();
    }
    // ssl
    if (g_http_server.https_port > 0) {
        std::string crt_file = ini.GetValue("ssl_certificate");
        std::string key_file = ini.GetValue("ssl_privatekey");
        std::string ca_file = ini.GetValue("ssl_ca_certificate");
        hlogi("SSL backend is %s", hssl_backend());
        hssl_ctx_init_param_t param;
        memset(&param, 0, sizeof(param));
        param.crt_file = crt_file.c_str();
        param.key_file = key_file.c_str();
        param.ca_file = ca_file.c_str();
        param.endpoint = HSSL_SERVER;
        if (hssl_ctx_init(&param) == NULL) {
            hloge("SSL certificate verify failed!");
            exit(0);
        }
        else {
            hlogi("SSL certificate verify ok!");
        }
    }

    hlogi("parse_confile('%s') OK", confile);
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

    // parse_confile
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
        signal_handle(signal);
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
    }
#endif

    // pidfile
    create_pidfile();

    // http_server
    Router::Register(g_http_service);
    g_http_server.registerHttpService(&g_http_service);

#if 0
    std::atomic_flag init_flag = ATOMIC_FLAG_INIT;
    g_http_server.onWorkerStart = [&init_flag](){
        if (!init_flag.test_and_set()) {
            hv::async::startup();
        }
    };
    g_http_server.onWorkerStop = [&init_flag](){
        if (init_flag.test_and_set()) {
            hv::async::cleanup();
        }
    };
#endif

    g_http_server.run();
    return ret;
}
