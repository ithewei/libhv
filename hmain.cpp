#include <signal.h> // for signal,kill...

#include "h.h"
#include "hsysinfo.h"

#include "hmain.h"
main_ctx_t g_main_ctx;

typedef struct conf_ctx_s {
    IniParser* parser;
    int loglevel;
    int worker_processes;
    int port;
} conf_ctx_t;
conf_ctx_t g_conf_ctx;

#define DEFAULT_WORKER_PROCESSES    4
#define MAXNUM_WORKER_PROCESSES     1024
inline void conf_ctx_init(conf_ctx_t* ctx) {
    ctx->parser = new IniParser;
    ctx->loglevel = LOG_LEVEL_DEBUG;
    ctx->worker_processes = 0;
    ctx->port = 0;
}

static int      create_pidfile();
static void     delete_pidfile();
static pid_t    getpid_from_pidfile();

static int  parse_cmdline(int argc, char** argv);
static void print_version();
static void print_help();
static void handle_signal();
static int  parse_confile(const char* confile);

static int  master_process_init();
static void master_process_exit();
static int  master_process_cycle();

static int  create_worker_processes(int worker_processes);
static int  worker_process_cycle(void* ctx);

int create_pidfile() {
    FILE* fp = fopen(g_main_ctx.pidfile, "w");
    if (fp == NULL) {
        printf("fopen [%s] error: %d\n", g_main_ctx.pidfile, errno);
        return -10;
    }

    char pid[16] = {0};
    snprintf(pid, sizeof(pid), "%d\n", g_main_ctx.pid);
    fwrite(pid, 1, strlen(pid), fp);
    fclose(fp); atexit(delete_pidfile); 
    hlogi("create_pidfile [%s] pid=%d", g_main_ctx.pidfile, g_main_ctx.pid);

    return 0;
}

void delete_pidfile() {
    remove(g_main_ctx.pidfile);
    hlogi("delete_pidfile [%s]", g_main_ctx.pidfile);
}

pid_t getpid_from_pidfile() {
    FILE* fp = fopen(g_main_ctx.pidfile, "r");
    if (fp == NULL) {
        //printf("fopen [%s] error: %d\n", g_conf_ctx.pidfile, errno);
        return -1;
    }
    char pid[64];
    int readbytes = fread(pid, 1, sizeof(pid), fp);
    fclose(fp);
    if (readbytes <= 0) {
        printf("fread [%s] bytes=%d\n", g_main_ctx.pidfile, readbytes);
        return -1;
    }
    return atoi(pid);
}

int main_ctx_init(int argc, char** argv) {
    g_main_ctx.pid = getpid();
    char* cwd = getcwd(g_main_ctx.run_path, sizeof(g_main_ctx.run_path));
    if (cwd == NULL) {
        printf("getcwd error\n");
    }
    //printf("run_path=%s\n", g_main_ctx.run_path);
    const char* b = argv[0];
    const char* e = b;
    while (*e) ++e;
    --e;
    while (e >= b) {
        if (*e == '/' || *e == '\\') {
            break;
        }
        --e;
    }
    strncpy(g_main_ctx.program_name, e+1, sizeof(g_main_ctx.program_name));
#ifdef _WIN32
    if (strcmp(g_main_ctx.program_name+strlen(g_main_ctx.program_name)-4, ".exe") == 0) {
        *(g_main_ctx.program_name+strlen(g_main_ctx.program_name)-4) = '\0';
    }
#endif
    //printf("program_name=%s\n", g_main_ctx.program_name);

    // save arg
    int i = 0;
    g_main_ctx.os_argv = argv;
    g_main_ctx.argc = 0;
    g_main_ctx.arg_len = 0;
    for (i = 0; argv[i]; ++i) {
        g_main_ctx.arg_len += strlen(argv[i]) + 1;
    }
    g_main_ctx.argc = i;
    char* argp = (char*)malloc(g_main_ctx.arg_len);
    memset(argp, 0, g_main_ctx.arg_len);
    g_main_ctx.save_argv = (char**)malloc((g_main_ctx.argc+1) * sizeof(char*));
    for (i = 0; argv[i]; ++i) {
        g_main_ctx.save_argv[i] = argp;
        strcpy(g_main_ctx.save_argv[i], argv[i]);
        argp += strlen(argv[i]) + 1;
    }
    g_main_ctx.save_argv[g_main_ctx.argc] = NULL;

    // save env
    g_main_ctx.os_envp = environ;
    g_main_ctx.envc = 0;
    g_main_ctx.env_len = 0;
    for (i = 0; environ[i]; ++i) {
        g_main_ctx.env_len += strlen(environ[i]) + 1;
    }
    g_main_ctx.envc = i;
    char* envp = (char*)malloc(g_main_ctx.env_len);
    memset(envp, 0, g_main_ctx.env_len);
    g_main_ctx.save_envp = (char**)malloc((g_main_ctx.envc+1) * sizeof(char*));
    for (i = 0; environ[i]; ++i) {
        g_main_ctx.save_envp[i] = envp;
        strcpy(g_main_ctx.save_envp[i], environ[i]);
        envp += strlen(environ[i]) + 1;
    }
    g_main_ctx.save_envp[g_main_ctx.envc] = NULL;

    // parse env
    for (i = 0; environ[i]; ++i) {
        char* b = environ[i];
        char* delim = strchr(b, '=');
        if (delim == NULL) {
            continue;
        }
        g_main_ctx.env_kv[std::string(b, delim-b)] = std::string(delim+1);
    }

    /*
    // print argv and envp
    printf("---------------arg------------------------------\n");
    for (auto& pair : g_main_ctx.arg_kv) {
        printf("%s=%s\n", pair.first.c_str(), pair.second.c_str());
    }
    printf("---------------env------------------------------\n");
    for (auto& pair : g_main_ctx.env_kv) {
        printf("%s=%s\n", pair.first.c_str(), pair.second.c_str());
    }

    printf("PWD=%s\n", get_env("PWD"));
    printf("USER=%s\n", get_env("USER"));
    printf("HOME=%s\n", get_env("HOME"));
    printf("LANG=%s\n", get_env("LANG"));
    printf("TERM=%s\n", get_env("TERM"));
    printf("SHELL=%s\n", get_env("SHELL"));
    printf("================================================\n");
    */

    char logpath[MAX_PATH] = {0};
    snprintf(logpath, sizeof(logpath), "%s/logs", g_main_ctx.run_path);
    MKDIR(logpath);
    snprintf(g_main_ctx.confile, sizeof(g_main_ctx.confile), "%s/etc/%s.conf", g_main_ctx.run_path, g_main_ctx.program_name);
    snprintf(g_main_ctx.pidfile, sizeof(g_main_ctx.pidfile), "%s/logs/%s.pid", g_main_ctx.run_path, g_main_ctx.program_name);
    snprintf(g_main_ctx.logfile, sizeof(g_main_ctx.confile), "%s/logs/%s.log", g_main_ctx.run_path, g_main_ctx.program_name);

    g_main_ctx.oldpid = getpid_from_pidfile();
#ifdef __unix__
    if (kill(g_main_ctx.oldpid, 0) == -1 && errno == ESRCH) {
        g_main_ctx.oldpid = -1;
    }
#else

#endif

    return 0;
}

const char* get_arg(const char* key) {
    auto iter = g_main_ctx.arg_kv.find(key);
    if (iter == g_main_ctx.arg_kv.end()) {
        return NULL;
    }
    return iter->second.c_str();
}

const char* get_env(const char* key) {
    auto iter = g_main_ctx.env_kv.find(key);
    if (iter == g_main_ctx.env_kv.end()) {
        return NULL;
    }
    return iter->second.c_str();
}

#ifdef __unix__
/*
 * memory layout
 * argv[0]\0argv[1]\0argv[n]\0env[0]\0env[1]\0env[n]\0
 */
void setproctitle(const char* title) {
    //printf("proctitle=%s\n", title);
    memset(g_main_ctx.os_argv[0], 0, g_main_ctx.arg_len + g_main_ctx.env_len);
    strncpy(g_main_ctx.os_argv[0], title, g_main_ctx.arg_len + g_main_ctx.env_len);
}
#endif

// unix short style
static char options[] = "hvc:ts:dp:";
static char detail_options[] = "\
-h              : print help\n\
-v              : print version\n\
-c  confile     : set configure file, default etc/${program}.conf\n\
-t              : test configure file and exit\n\
-s  signal      : send signal to process\n\
                  signal=[start, stop, restart, status]\n\
-d              : daemon\n\
-p  port        : set listen port\n\
";

void print_version() {
    printf("%s version %s\n", g_main_ctx.program_name, get_compile_version());
}

void print_help() {
    printf("Usage: %s [%s]\n", g_main_ctx.program_name, options);
    printf("Options:\n%s\n", detail_options);
}

#define INVALID_OPTION  -1
#define FLAG_OPTION     1
#define PARMA_OPTION    2
int get_option(char opt) {
    char* p = options;
    while (*p && *p != opt) ++p;
    if (*p == '\0')     return INVALID_OPTION;
    if (*(p+1) == ':')  return PARMA_OPTION;
    return FLAG_OPTION;
}

int parse_cmdline(int argc, char** argv) {
    int i = 1;
    while (argv[i]) {
        char* p = argv[i];
        if (*p != '-') {
            printf("Invalid argv[%d]: %s\n", i, argv[i]);
            exit(-10);
        }
        while (*++p) {
            switch (get_option(*p)) {
            case INVALID_OPTION:
                printf("Invalid option: '%c'\n", *p);
                exit(-20);
            case FLAG_OPTION:
                g_main_ctx.arg_kv[std::string(p, 1)] = "true";
                break;
            case PARMA_OPTION:
                if (*(p+1) != '\0') {
                    g_main_ctx.arg_kv[std::string(p, 1)] = p+1;
                    ++i;
                    goto next_option;
                } else if (argv[i+1] != NULL) {
                    g_main_ctx.arg_kv[std::string(p, 1)] = argv[i+1];
                    i += 2;
                    goto next_option;
                } else {
                    printf("Option '%c' requires param\n", *p);
                    exit(-30);
                }
            }
        }
        ++i;
next_option:
        continue;
    }
    return 0;
}

int parse_confile(const char* confile) {
    conf_ctx_init(&g_conf_ctx);
    int ret = g_conf_ctx.parser->LoadFromFile(confile);
    if (ret != 0) {
        printf("Load confile [%s] failed: %d\n", confile, ret);
        exit(-40);
    }

    // loglevel
    const char* szLoglevel = g_conf_ctx.parser->GetValue("loglevel").c_str();
    if (stricmp(szLoglevel, "DEBUG") == 0) {
        g_conf_ctx.loglevel = LOG_LEVEL_DEBUG;
    } else if (stricmp(szLoglevel, "INFO") == 0) {
        g_conf_ctx.loglevel = LOG_LEVEL_INFO;
    } else if (stricmp(szLoglevel, "WARN") == 0) {
        g_conf_ctx.loglevel = LOG_LEVEL_WARN;
    } else if (stricmp(szLoglevel, "ERROR") == 0) {
        g_conf_ctx.loglevel = LOG_LEVEL_ERROR;
    } else {
        g_conf_ctx.loglevel = LOG_LEVEL_DEBUG;
    }
    hlog_set_level(g_conf_ctx.loglevel);

    // worker_processes
    int worker_processes = 0;
    worker_processes = atoi(g_conf_ctx.parser->GetValue("worker_processes").c_str());
    if (worker_processes <= 0 || worker_processes > MAXNUM_WORKER_PROCESSES) {
        worker_processes = get_ncpu();
        hlogd("worker_processes=ncpu=%d", worker_processes);
    }
    if (worker_processes <= 0 || worker_processes > MAXNUM_WORKER_PROCESSES) {
        worker_processes = DEFAULT_WORKER_PROCESSES;
    }
    g_conf_ctx.worker_processes = worker_processes;

    // port
    int port = 0;
    port = atoi(g_conf_ctx.parser->GetValue("port").c_str());
    if (port == 0) {
        port = atoi(get_arg("p"));
    }
    if (port == 0) {
        printf("Please config listen port!\n");
        exit(-10);
    }
    g_conf_ctx.port = port;

    return 0;
}

int master_process_cycle() {
    while (1) msleep(1);
    return 0;
}

int worker_process_cycle(void* ctx) {
    while (1) msleep(1);
    return 0;
}

#ifdef __unix__
// unix use signal
// unix use multi-processes
// we use SIGTERM to quit process
#define SIGNAL_TERMINATE    SIGTERM
#include <sys/wait.h>

static pid_t s_worker_processes[MAXNUM_WORKER_PROCESSES];

int create_worker_processes(int worker_processes) {
    for (int i = 0; i < worker_processes; ++i) {
        pid_t pid = fork();
        if (pid < 0) {
            hloge("fork error: %d", errno);
            return errno;
        }
        if (pid == 0) {
            hlogi("worker process start/running, pid=%d", getpid());
            char proctitle[256] = {0};
            snprintf(proctitle, sizeof(proctitle), "%s: worker process", g_main_ctx.program_name);
            setproctitle(proctitle);

            long port = g_conf_ctx.port + i + 1;
            worker_process_cycle((void*)port);
            exit(0);
        }

        for (int i = 0; i < MAXNUM_WORKER_PROCESSES; ++i) {
            if (s_worker_processes[i] <= 0) {
                s_worker_processes[i] = pid;
                break;
            }
        }
    }
    return 0;
}

void master_process_signal_handler(int signo) {
    hlogi("pid=%d recv signo=%d", getpid(), signo);
    switch (signo) {
    case SIGINT:
    case SIGNAL_TERMINATE:
        hlogi("killall worker processes");
        signal(SIGCHLD, SIG_IGN);
        for (int i = 0; i < MAXNUM_WORKER_PROCESSES; ++i) {
            if (s_worker_processes[i] <= 0) break;
            kill(s_worker_processes[i], SIGKILL);
            s_worker_processes[i] = -1;
        }
        exit(0);
        break;
    case SIGCHLD:
    {
        pid_t pid = 0;
        int status = 0;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            hlogw("worker process stop/waiting, pid=%d status=%d", pid, status);
            for (int i = 0; i < MAXNUM_WORKER_PROCESSES; ++i) {
                if (s_worker_processes[i] == pid) {
                    s_worker_processes[i] = -1;
                    break;
                }
            }
            create_worker_processes(1);
        }
    }
        break;
    default:
        break;
    }
}

int master_process_init() {
    char proctitle[256] = {0};
    snprintf(proctitle, sizeof(proctitle), "%s: master process", g_main_ctx.program_name);
    setproctitle(proctitle);

    signal(SIGINT, master_process_signal_handler);
    signal(SIGCHLD, master_process_signal_handler);
    signal(SIGNAL_TERMINATE, master_process_signal_handler);

    for (int i = 0; i < MAXNUM_WORKER_PROCESSES; ++i) {
        s_worker_processes[i] = -1;
    }

    atexit(master_process_exit);
    return 0;
}

void master_process_exit() {
}
#elif defined(_WIN32)
// win32 use Event
// win32 use multi-threads
static HANDLE s_hEventTerm = NULL;

#include <mmsystem.h>
#ifdef _MSC_VER
#pragma comment(lib, "winmm.lib")
#endif
void WINAPI on_timer(UINT uTimerID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2) {
    DWORD ret = WaitForSingleObject(s_hEventTerm, 0);
    if (ret == WAIT_OBJECT_0) {
        timeKillEvent(uTimerID);
        hlogi("pid=%d recv event [TERM]", getpid());
        exit(0);
    }
}

int master_process_init() {
    char eventname[MAX_PATH] = {0};
    snprintf(eventname, sizeof(eventname), "%s_term_event", g_main_ctx.program_name);
    s_hEventTerm = CreateEvent(NULL, FALSE, FALSE, eventname);
    //s_hEventTerm = OpenEvent(EVENT_ALL_ACCESS, FALSE, eventname);

    timeSetEvent(1000, 1000, on_timer, 0, TIME_PERIODIC);

    atexit(master_process_exit);
    return 0;
}

void master_process_exit() {
    CloseHandle(s_hEventTerm);
    s_hEventTerm = NULL;
}

#include <process.h>
void thread_proc(void* ctx) {
    hlogi("worker thread start/running, tid=%d", gettid());
    worker_process_cycle(ctx);
}

int create_worker_processes(int worker_processes) {
    for (int i = 0; i < worker_processes; ++i) {
        long port = g_conf_ctx.port + i + 1;
        _beginthread(thread_proc, 0, (void*)port);
    }
    return 0;
}
#endif

void handle_signal() {
    const char* signal = get_arg("s");
    if (signal) {
        if (strcmp(signal, "start") == 0) {
            if (g_main_ctx.oldpid > 0) {
                printf("%s is already running, pid=%d\n", g_main_ctx.program_name, g_main_ctx.oldpid);
                exit(0);
            }
        } else if (strcmp(signal, "stop") == 0) {
            if (g_main_ctx.oldpid > 0) {
#ifdef __unix__
                kill(g_main_ctx.oldpid, SIGNAL_TERMINATE);
#else
                SetEvent(s_hEventTerm);
#endif
                printf("%s stop/waiting\n", g_main_ctx.program_name);
            } else {
                printf("%s is already stopped\n", g_main_ctx.program_name);
            }
            exit(0);
        } else if (strcmp(signal, "restart") == 0) {
            if (g_main_ctx.oldpid > 0) {
#ifdef __unix__
                kill(g_main_ctx.oldpid, SIGNAL_TERMINATE);
#else
                SetEvent(s_hEventTerm);
#endif
                printf("%s stop/waiting\n", g_main_ctx.program_name);
                msleep(1000);
            }
        } else if (strcmp(signal, "status") == 0) {
            if (g_main_ctx.oldpid > 0) {
                printf("%s start/running, pid=%d\n", g_main_ctx.program_name, g_main_ctx.oldpid);
            } else {
                printf("%s stop/waiting\n", g_main_ctx.program_name);
            }
            exit(0);
        } else {
            printf("Invalid signal: '%s'\n", signal);
            exit(0);
        }
        printf("%s start/running\n", g_main_ctx.program_name);
    }
}

int main(int argc, char** argv) {
    // g_main_ctx
    main_ctx_init(argc, argv);
    parse_cmdline(argc, argv);

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

    // logfile
    hlog_set_file(g_main_ctx.logfile);
    hlogi("%s version: %s", g_main_ctx.program_name, get_compile_version());

    // confile
    const char* confile = get_arg("c");
    if (confile) {
        strncpy(g_main_ctx.confile, confile, sizeof(g_main_ctx.confile));
    }

    // g_conf_ctx
    parse_confile(g_main_ctx.confile);

    // test
    if (get_arg("t")) {
        printf("Test confile [%s] OK!\n", g_main_ctx.confile);
        exit(0);
    }

    master_process_init();

    // signal
    handle_signal();

#ifdef __unix__
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
    hlogi("%s start/running, pid=%d", g_main_ctx.program_name, g_main_ctx.pid);

    // cycle
    create_worker_processes(g_conf_ctx.worker_processes);
    master_process_cycle();

    return 0;
}

