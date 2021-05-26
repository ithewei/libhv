/*
 * @介绍：httpd即HTTP服务端后台程序，该示例程序展示了如何使用libhv构建一个功能完备的HTTP服务端。
 *        打算使用libhv做HTTP服务端的同学建议通读此程序，你将受益匪浅。
 *
 */

#include "hv.h"
#include "hmain.h"
#include "iniparser.h"

#include "HttpServer.h"

#include "router.h"

http_server_t   g_http_server;
HttpService     g_http_service;

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

// 解析配置文件
int parse_confile(const char* confile) {
    // 加载配置文件
    IniParser ini;
    int ret = ini.LoadFromFile(confile);
    if (ret != 0) {
        printf("Load confile [%s] failed: %d\n", confile, ret);
        exit(-40);
    }

    // 设置日志文件
    // logfile
    string str = ini.GetValue("logfile");
    if (!str.empty()) {
        strncpy(g_main_ctx.logfile, str.c_str(), sizeof(g_main_ctx.logfile));
    }
    hlog_set_file(g_main_ctx.logfile);

    // 设置日志等级
    // loglevel
    str = ini.GetValue("loglevel");
    if (!str.empty()) {
        hlog_set_level_by_str(str.c_str());
    }

    // 设置日志文件大小
    // log_filesize
    str = ini.GetValue("log_filesize");
    if (!str.empty()) {
        hlog_set_max_filesize_by_str(str.c_str());
    }

    // 设置日志保留天数
    // log_remain_days
    str = ini.GetValue("log_remain_days");
    if (!str.empty()) {
        hlog_set_remain_days(atoi(str.c_str()));
    }

    // 是否启用fsync强制刷新日志缓存到磁盘
    // log_fsync
    str = ini.GetValue("log_fsync");
    if (!str.empty()) {
        logger_enable_fsync(hlog, getboolean(str.c_str()));
    }
    hlogi("%s version: %s", g_main_ctx.program_name, hv_compile_version());
    hlog_fsync();

    // 设置工作进程数
    // worker_processes
    int worker_processes = 0;
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
    g_http_server.worker_processes = LIMIT(0, worker_processes, MAXNUM_WORKER_PROCESSES);

    // 设置工作进程数
    // worker_threads
    int worker_threads = ini.Get<int>("worker_threads");
    g_http_server.worker_threads = LIMIT(0, worker_threads, 16);

    // 设置http监听端口
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

    // 设置https监听端口
    // https_port
    if (HV_WITH_SSL) {
        g_http_server.https_port = ini.Get<int>("https_port");
    }
    if (g_http_server.port == 0 && g_http_server.https_port == 0) {
        printf("Please config listen port!\n");
        exit(-10);
    }

    // 设置url前缀
    // base_url
    str = ini.GetValue("base_url");
    if (str.size() != 0) {
        g_http_service.base_url = str;
    }

    // 设置html文档根目录
    // document_root
    str = ini.GetValue("document_root");
    if (str.size() != 0) {
        g_http_service.document_root = str;
    }

    // 设置首页
    // home_page
    str = ini.GetValue("home_page");
    if (str.size() != 0) {
        g_http_service.home_page = str;
    }

    // 设置错误页面
    // error_page
    str = ini.GetValue("error_page");
    if (str.size() != 0) {
        g_http_service.error_page = str;
    }

    // 设置indexof目录
    // index_of
    str = ini.GetValue("index_of");
    if (str.size() != 0) {
        g_http_service.index_of = str;
    }

    // 设置SSL证书
    // ssl
    if (g_http_server.https_port > 0) {
        std::string crt_file = ini.GetValue("ssl_certificate");
        std::string key_file = ini.GetValue("ssl_privatekey");
        std::string ca_file = ini.GetValue("ssl_ca_certificate");
        hssl_ctx_init_param_t param;
        memset(&param, 0, sizeof(param));
        param.crt_file = crt_file.c_str();
        param.key_file = key_file.c_str();
        param.ca_file = ca_file.c_str();
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

// reload信号处理: 重新加载配置文件
static void on_reload(void* userdata) {
    hlogi("reload confile [%s]", g_main_ctx.confile);
    parse_confile(g_main_ctx.confile);
}

int main(int argc, char** argv) {
    // g_main_ctx
    main_ctx_init(argc, argv);
    // 解析命令行
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

    // -h 打印帮助信息
    // help
    if (get_arg("h")) {
        print_help();
        exit(0);
    }

    // -v 打印版本信息
    // version
    if (get_arg("v")) {
        print_version();
        exit(0);
    }

    // -c 设置配置文件
    // parse_confile
    const char* confile = get_arg("c");
    if (confile) {
        strncpy(g_main_ctx.confile, confile, sizeof(g_main_ctx.confile));
    }
    // 解析配置文件
    parse_confile(g_main_ctx.confile);

    // -t 测试配置文件
    // test
    if (get_arg("t")) {
        printf("Test confile [%s] OK!\n", g_main_ctx.confile);
        exit(0);
    }

    // -s 信号处理
    // signal
    signal_init(on_reload);
    const char* signal = get_arg("s");
    if (signal) {
        signal_handle(signal);
    }

#ifdef OS_UNIX
    // -d 后台运行
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

    // 创建pid文件
    // pidfile
    create_pidfile();

    // http_server
    // 注册路由
    Router::Register(g_http_service);
    g_http_server.service = &g_http_service;
    // 运行http服务
    ret = http_server_run(&g_http_server);
    return ret;
}
