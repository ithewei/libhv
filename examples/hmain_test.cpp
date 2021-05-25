#include "hv.h"
#include "hmain.h"
#include "iniparser.h"

/*
 * @build: make examples
 * @usage: bin/hmain_test -h
 *         bin/hmain_test -v
 *
 *         bin/hmain_test -c etc/hmain_test.conf -d
 *         ps aux | grep hmain_test
 *
 *         bin/hmain_test -s stop
 *         ps aux | grep hmain_test
 *
 */

typedef struct conf_ctx_s {
    IniParser* parser;
    int loglevel;
    int worker_processes;
    int worker_threads;
    int port;
} conf_ctx_t;
conf_ctx_t g_conf_ctx;

inline void conf_ctx_init(conf_ctx_t* ctx) {
    ctx->parser = new IniParser;
    ctx->loglevel = LOG_LEVEL_DEBUG;
    ctx->worker_processes = 0;
    ctx->worker_threads = 0;
    ctx->port = 0;
}

static void print_version();
static void print_help();

static int  parse_confile(const char* confile);
static void worker_fn(void* userdata);

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
    str = g_conf_ctx.parser->GetValue("loglevel");
    if (!str.empty()) {
        hlog_set_level_by_str(str.c_str());
    }
    // log_filesize
    str = g_conf_ctx.parser->GetValue("log_filesize");
    if (!str.empty()) {
        hlog_set_max_filesize_by_str(str.c_str());
    }
    // log_remain_days
    str = g_conf_ctx.parser->GetValue("log_remain_days");
    if (!str.empty()) {
        hlog_set_remain_days(atoi(str.c_str()));
    }
    // log_fsync
    str = g_conf_ctx.parser->GetValue("log_fsync");
    if (!str.empty()) {
        logger_enable_fsync(hlog, getboolean(str.c_str()));
    }
    // first log here
    hlogi("%s version: %s", g_main_ctx.program_name, hv_compile_version());
    hlog_fsync();

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
    // worker_threads
    int worker_threads = g_conf_ctx.parser->Get<int>("worker_threads");
    g_conf_ctx.worker_threads = LIMIT(0, worker_threads, 16);

    // port
    int port = 0;
    const char* szPort = get_arg("p");
    if (szPort) {
        port = atoi(szPort);
    }
    if (port == 0) {
        port = g_conf_ctx.parser->Get<int>("port");
    }
    if (port == 0) {
        printf("Please config listen port!\n");
        exit(-10);
    }
    g_conf_ctx.port = port;

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
    if (argc == 1) {
        print_help();
        exit(10);
    }
    // int ret = parse_opt(argc, argv, options);
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

    master_workers_run(worker_fn, (void*)(intptr_t)g_conf_ctx.port, g_conf_ctx.worker_processes, g_conf_ctx.worker_threads);

    return 0;
}

void worker_fn(void* userdata) {
    long port = (long)(intptr_t)(userdata);
    while (1) {
        printf("port=%ld pid=%ld tid=%ld\n", port, hv_getpid(), hv_gettid());
        hv_delay(60000);
    }
}
