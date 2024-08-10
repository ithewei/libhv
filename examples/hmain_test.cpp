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

// long options
static const option_t long_options[] = {
    {'h', "help",       NO_ARGUMENT,        "Print this information"},
    {'v', "version",    NO_ARGUMENT,        "Print version"},
    {'c', "confile",    REQUIRED_ARGUMENT,  "Set configure file, default etc/{program}.conf"},
    {'t', "test",       NO_ARGUMENT,        "Test configure file and exit"},
    {'s', "signal",     REQUIRED_ARGUMENT,  "send signal to process, signal=[start,stop,restart,status,reload]"},
    {'d', "daemon",     NO_ARGUMENT,        "Daemonize"},
    {'p', "port",       REQUIRED_ARGUMENT,  "Set listen port"}
};

void print_version() {
    printf("%s version %s\n", g_main_ctx.program_name, hv_compile_version());
}

void print_help() {
    char detail_options[1024] = {0};
    dump_opt_long(long_options, ARRAY_SIZE(long_options), detail_options, sizeof(detail_options));
    printf("%s\n", detail_options);
}

int parse_confile(const char* confile) {
    int ret = g_conf_ctx.parser->LoadFromFile(confile);
    if (ret != 0) {
        printf("Load confile [%s] failed: %d\n", confile, ret);
        exit(-40);
    }

    // logfile
    std::string str = g_conf_ctx.parser->GetValue("logfile");
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
        logger_enable_fsync(hlog, hv_getboolean(str.c_str()));
    }
    // first log here
    hlogi("%s version: %s", g_main_ctx.program_name, hv_compile_version());
    hlog_fsync();

    // worker_processes
    int worker_processes = 0;
#ifdef DEBUG
    // Disable multi-processes mode for debugging
    worker_processes = 0;
#else
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
#endif
    g_conf_ctx.worker_processes = LIMIT(0, worker_processes, MAXNUM_WORKER_PROCESSES);
    // worker_threads
    int worker_threads = 0;
    str = g_conf_ctx.parser->GetValue("worker_threads");
    if (str.size() != 0) {
        if (strcmp(str.c_str(), "auto") == 0) {
            worker_threads = get_ncpu();
            hlogd("worker_threads=ncpu=%d", worker_threads);
        }
        else {
            worker_threads = atoi(str.c_str());
        }
    }
    g_conf_ctx.worker_threads = LIMIT(0, worker_threads, 64);

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
    int ret = parse_opt_long(argc, argv, long_options, ARRAY_SIZE(long_options));
    if (ret != 0) {
        print_help();
        exit(ret);
    }

    /*
    printf("---------------arg------------------------------\n");
    printf("%s\n", g_main_ctx.cmdline);
    for (int i = 0; i < g_main_ctx.arg_kv_size; ++i) {
        printf("%s\n", g_main_ctx.arg_kv[i]);
    }
    for (int i = 0; i < g_main_ctx.arg_list_size; ++i) {
        printf("%s\n", g_main_ctx.arg_list[i]);
    }
    printf("================================================\n");

    printf("---------------env------------------------------\n");
    for (int i = 0; i < g_main_ctx.envc; ++i) {
        printf("%s\n", g_main_ctx.save_envp[i]);
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
