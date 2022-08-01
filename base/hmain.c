#include "hmain.h"

#include "hbase.h"
#include "hlog.h"
#include "herr.h"
#include "htime.h"
#include "hthread.h"

#ifdef OS_DARWIN
#include <crt_externs.h>
#define environ (*_NSGetEnviron())
#endif

main_ctx_t  g_main_ctx;

static void init_arg_kv(int maxsize) {
    g_main_ctx.arg_kv_size = 0;
    SAFE_ALLOC(g_main_ctx.arg_kv, sizeof(char*) * maxsize);
}

static void save_arg_kv(const char* key, int key_len, const char* val, int val_len) {
    if (key_len <= 0) key_len = strlen(key);
    if (val_len <= 0) val_len = strlen(val);
    char* arg = NULL;
    SAFE_ALLOC(arg, key_len + val_len + 2);
    memcpy(arg, key, key_len);
    arg[key_len] = '=';
    memcpy(arg + key_len + 1, val, val_len);
    // printf("save_arg_kv: %s\n", arg);
    g_main_ctx.arg_kv[g_main_ctx.arg_kv_size++] = arg;
}

static void init_arg_list(int maxsize) {
    g_main_ctx.arg_list_size = 0;
    SAFE_ALLOC(g_main_ctx.arg_list, sizeof(char*) * maxsize);
}

static void save_arg_list(const char* arg) {
    // printf("save_arg_list: %s\n", arg);
    g_main_ctx.arg_list[g_main_ctx.arg_list_size++] = strdup(arg);
}

static const char* get_val(char** kvs, const char* key) {
    if (kvs == NULL) return NULL;
    int key_len = strlen(key);
    char* kv = NULL;
    int kv_len = 0;
    for (int i = 0; kvs[i]; ++i) {
        kv = kvs[i];
        kv_len = strlen(kv);
        if (kv_len <= key_len) continue;
        // key=val
        if (memcmp(kv, key, key_len) == 0 && kv[key_len] == '=') {
            return kv + key_len + 1;
        }
    }
    return NULL;
}

const char* get_arg(const char* key) {
    return get_val(g_main_ctx.arg_kv, key);
}

const char* get_env(const char* key) {
    return get_val(g_main_ctx.save_envp, key);
}

int main_ctx_init(int argc, char** argv) {
    if (argc == 0 || argv == NULL) {
        argc = 1;
        SAFE_ALLOC(argv, 2 * sizeof(char*));
        SAFE_ALLOC(argv[0], MAX_PATH);
        get_executable_path(argv[0], MAX_PATH);
    }

    get_run_dir(g_main_ctx.run_dir, sizeof(g_main_ctx.run_dir));
    //printf("run_dir=%s\n", g_main_ctx.run_dir);
    strncpy(g_main_ctx.program_name, hv_basename(argv[0]), sizeof(g_main_ctx.program_name));
#ifdef OS_WIN
    if (strcmp(g_main_ctx.program_name+strlen(g_main_ctx.program_name)-4, ".exe") == 0) {
        *(g_main_ctx.program_name+strlen(g_main_ctx.program_name)-4) = '\0';
    }
#endif
    //printf("program_name=%s\n", g_main_ctx.program_name);
    char logdir[MAX_PATH] = {0};
    snprintf(logdir, sizeof(logdir), "%s/logs", g_main_ctx.run_dir);
    hv_mkdir(logdir);
    snprintf(g_main_ctx.confile, sizeof(g_main_ctx.confile), "%s/etc/%s.conf", g_main_ctx.run_dir, g_main_ctx.program_name);
    snprintf(g_main_ctx.pidfile, sizeof(g_main_ctx.pidfile), "%s/logs/%s.pid", g_main_ctx.run_dir, g_main_ctx.program_name);
    snprintf(g_main_ctx.logfile, sizeof(g_main_ctx.confile), "%s/logs/%s.log", g_main_ctx.run_dir, g_main_ctx.program_name);
    hlog_set_file(g_main_ctx.logfile);

    g_main_ctx.pid = getpid();
    g_main_ctx.oldpid = getpid_from_pidfile();
#ifdef OS_UNIX
    if (kill(g_main_ctx.oldpid, 0) == -1 && errno == ESRCH) {
        g_main_ctx.oldpid = -1;
    }
#else
    HANDLE hproc = OpenProcess(PROCESS_TERMINATE, FALSE, g_main_ctx.oldpid);
    if (hproc == NULL) {
        g_main_ctx.oldpid = -1;
    }
    else {
        CloseHandle(hproc);
    }
#endif

    // save arg
    int i = 0;
    g_main_ctx.os_argv = argv;
    g_main_ctx.argc = 0;
    g_main_ctx.arg_len = 0;
    for (i = 0; argv[i]; ++i) {
        g_main_ctx.arg_len += strlen(argv[i]) + 1;
    }
    g_main_ctx.argc = i;
    char* argp = NULL;
    SAFE_ALLOC(argp, g_main_ctx.arg_len);
    SAFE_ALLOC(g_main_ctx.save_argv, (g_main_ctx.argc + 1) * sizeof(char*));
    char* cmdline = NULL;
    SAFE_ALLOC(cmdline, g_main_ctx.arg_len);
    g_main_ctx.cmdline = cmdline;
    for (i = 0; argv[i]; ++i) {
        strcpy(argp, argv[i]);
        g_main_ctx.save_argv[i] = argp;
        argp += strlen(argv[i]) + 1;

        strcpy(cmdline, argv[i]);
        cmdline += strlen(argv[i]);
        *cmdline = ' ';
        ++cmdline;
    }
    g_main_ctx.save_argv[g_main_ctx.argc] = NULL;
    g_main_ctx.cmdline[g_main_ctx.arg_len-1] = '\0';

#if defined(OS_WIN) || defined(OS_LINUX) || defined(OS_DARWIN)
    // save env
    g_main_ctx.os_envp = environ;
    g_main_ctx.envc = 0;
    g_main_ctx.env_len = 0;
    for (i = 0; environ[i]; ++i) {
        g_main_ctx.env_len += strlen(environ[i]) + 1;
    }
    g_main_ctx.envc = i;
    char* envp = NULL;
    SAFE_ALLOC(envp, g_main_ctx.env_len);
    SAFE_ALLOC(g_main_ctx.save_envp, (g_main_ctx.envc + 1) * sizeof(char*));
    for (i = 0; environ[i]; ++i) {
        g_main_ctx.save_envp[i] = envp;
        strcpy(g_main_ctx.save_envp[i], environ[i]);
        envp += strlen(environ[i]) + 1;
    }
    g_main_ctx.save_envp[g_main_ctx.envc] = NULL;
#endif

    // signals
    g_main_ctx.reload_fn = NULL;
    g_main_ctx.reload_userdata = NULL;

    // master workers
    g_main_ctx.worker_processes = 0;
    g_main_ctx.worker_threads = 0;
    g_main_ctx.worker_fn = 0;
    g_main_ctx.worker_userdata = 0;
    g_main_ctx.proc_ctxs = NULL;

    atexit(main_ctx_free);
    return 0;
}

void main_ctx_free(void) {
    if (g_main_ctx.save_argv) {
        SAFE_FREE(g_main_ctx.save_argv[0]);
        SAFE_FREE(g_main_ctx.save_argv);
    }
    SAFE_FREE(g_main_ctx.cmdline);
    if (g_main_ctx.save_envp) {
        SAFE_FREE(g_main_ctx.save_envp[0]);
        SAFE_FREE(g_main_ctx.save_envp);
    }
    if (g_main_ctx.arg_kv) {
        for (int i = 0; i < g_main_ctx.arg_kv_size; ++i) {
            SAFE_FREE(g_main_ctx.arg_kv[i]);
        }
        SAFE_FREE(g_main_ctx.arg_kv);
    }
    if (g_main_ctx.arg_list) {
        for (int i = 0; i < g_main_ctx.arg_list_size; ++i) {
            SAFE_FREE(g_main_ctx.arg_list[i]);
        }
        SAFE_FREE(g_main_ctx.arg_list);
    }
}

#define UNDEFINED_OPTION    -1
static int get_arg_type(int short_opt, const char* options) {
    if (options == NULL) return UNDEFINED_OPTION;
    const char* p = options;
    while (*p && *p != short_opt) ++p;
    if (*p == '\0')     return UNDEFINED_OPTION;
    if (*(p+1) == ':')  return REQUIRED_ARGUMENT;
    return NO_ARGUMENT;
}

int parse_opt(int argc, char** argv, const char* options) {
    if (argc < 1) return 0;
    init_arg_kv(strlen(options) + 1);
    init_arg_list(argc);

    for (int i = 1; argv[i]; ++i) {
        char* p = argv[i];
        if (*p != '-') {
            save_arg_list(argv[i]);
            continue;
        }
        while (*++p) {
            int arg_type = get_arg_type(*p, options);
            if (arg_type == UNDEFINED_OPTION) {
                printf("Invalid option '%c'\n", *p);
                return -20;
            } else if (arg_type == NO_ARGUMENT) {
                save_arg_kv(p, 1, OPTION_ENABLE, 0);
                continue;
            } else if (arg_type == REQUIRED_ARGUMENT) {
                if (*(p+1) != '\0') {
                    save_arg_kv(p, 1, p+1, 0);
                    break;
                } else if (argv[i+1] != NULL) {
                    save_arg_kv(p, 1, argv[++i], 0);
                    break;
                } else {
                    printf("Option '%c' requires param\n", *p);
                    return -30;
                }
            }
        }
    }
    return 0;
}

static const option_t* get_option(const char* opt, const option_t* long_options, int size) {
    if (opt == NULL || long_options == NULL) return NULL;
    int len = strlen(opt);
    if (len == 0)   return NULL;
    if (len == 1) {
        for (int i = 0; i < size; ++i) {
            if (long_options[i].short_opt == *opt) {
                return &long_options[i];
            }
        }
    } else {
        for (int i = 0; i < size; ++i) {
            if (strcmp(long_options[i].long_opt, opt) == 0) {
                return &long_options[i];
            }
        }
    }

    return NULL;
}

#define MAX_OPTION      32
// opt type
#define NOPREFIX_OPTION 0
#define SHORT_OPTION    -1
#define LONG_OPTION     -2
int parse_opt_long(int argc, char** argv, const option_t* long_options, int size) {
    if (argc < 1) return 0;
    init_arg_kv(size + 1);
    init_arg_list(argc);

    char opt[MAX_OPTION+1] = {0};
    for (int i = 1; argv[i]; ++i) {
        char* arg = argv[i];
        int opt_type = NOPREFIX_OPTION;
        // prefix
        if (*arg == OPTION_PREFIX) {
            ++arg;
            opt_type = SHORT_OPTION;
            if (*arg == OPTION_PREFIX) {
                ++arg;
                opt_type = LONG_OPTION;
            }
        }
        int arg_len  = strlen(arg);
        // delim
        char* delim = strchr(arg, OPTION_DELIM);
        if (delim) {
            if (delim == arg || delim == arg+arg_len-1 || delim-arg > MAX_OPTION) {
                printf("Invalid option '%s'\n", argv[i]);
                return -10;
            }
            memcpy(opt, arg, delim-arg);
            opt[delim-arg] = '\0';
        } else {
            if (opt_type == SHORT_OPTION) {
                *opt = *arg;
                opt[1] = '\0';
            } else {
                strncpy(opt, arg, MAX_OPTION);
            }
        }
        // get_option
        const option_t* pOption = get_option(opt, long_options, size);
        if (pOption == NULL) {
            if (delim == NULL && opt_type == NOPREFIX_OPTION) {
                save_arg_list(arg);
                continue;
            } else {
                printf("Invalid option: '%s'\n", argv[i]);
                return -10;
            }
        }
        const char* value = NULL;
        if (pOption->arg_type == NO_ARGUMENT) {
            // -h
            value = OPTION_ENABLE;
        } else if (pOption->arg_type == REQUIRED_ARGUMENT) {
            if (delim) {
                // --port=80
                value = delim+1;
            } else {
                if (opt_type == SHORT_OPTION && *(arg+1) != '\0') {
                    // p80
                    value = arg+1;
                } else if (argv[i+1] != NULL) {
                    // --port 80
                    value = argv[++i];
                } else {
                    printf("Option '%s' requires parament\n", opt);
                    return -20;
                }
            }
        }
        // preferred to use short_opt as key
        if (pOption->short_opt > 0) {
            save_arg_kv(&pOption->short_opt, 1, value, 0);
        } else if (pOption->long_opt) {
            save_arg_kv(pOption->long_opt, 0, value, 0);
        }
    }
    return 0;
}

#if defined(OS_UNIX) && !HAVE_SETPROCTITLE
/*
 * memory layout
 * argv[0]\0argv[1]\0argv[n]\0env[0]\0env[1]\0env[n]\0
 */
void setproctitle(const char* fmt, ...) {
    char buf[256] = {0};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);

    int len = g_main_ctx.arg_len + g_main_ctx.env_len;
    if (g_main_ctx.os_argv && len) {
        strncpy(g_main_ctx.os_argv[0], buf, len-1);
    }
}
#endif

int create_pidfile() {
    FILE* fp = fopen(g_main_ctx.pidfile, "w");
    if (fp == NULL) {
        hloge("fopen('%s') error: %d", g_main_ctx.pidfile, errno);
        return -1;
    }

    g_main_ctx.pid = hv_getpid();
    fprintf(fp, "%d\n", (int)g_main_ctx.pid);
    fclose(fp);
    hlogi("create_pidfile('%s') pid=%d", g_main_ctx.pidfile, g_main_ctx.pid);
    atexit(delete_pidfile);
    return 0;
}

void delete_pidfile(void) {
    hlogi("delete_pidfile('%s') pid=%d", g_main_ctx.pidfile, g_main_ctx.pid);
    remove(g_main_ctx.pidfile);
}

pid_t getpid_from_pidfile() {
    FILE* fp = fopen(g_main_ctx.pidfile, "r");
    if (fp == NULL) {
        // hloge("fopen('%s') error: %d", g_main_ctx.pidfile, errno);
        return -1;
    }
    int pid = -1;
    fscanf(fp, "%d", &pid);
    fclose(fp);
    return pid;
}

#ifdef OS_UNIX
// unix use signal
#include <sys/wait.h>

void signal_handler(int signo) {
    hlogi("pid=%d recv signo=%d", getpid(), signo);
    switch (signo) {
    case SIGINT:
    case SIGNAL_TERMINATE:
        hlogi("killall processes");
        signal(SIGCHLD, SIG_IGN);
        // master send SIGKILL => workers
        for (int i = 0; i < g_main_ctx.worker_processes; ++i) {
            if (g_main_ctx.proc_ctxs[i].pid <= 0) break;
            kill(g_main_ctx.proc_ctxs[i].pid, SIGKILL);
            g_main_ctx.proc_ctxs[i].pid = -1;
        }
        exit(0);
        break;
    case SIGCHLD:
    {
        pid_t pid = 0;
        int status = 0;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            hlogw("proc stop/waiting, pid=%d status=%d", pid, status);
            for (int i = 0; i < g_main_ctx.worker_processes; ++i) {
                proc_ctx_t* ctx = g_main_ctx.proc_ctxs + i;
                if (ctx->pid == pid) {
                    ctx->pid = -1;
                    // NOTE: avoid frequent crash and restart
                    time_t run_time = time(NULL) - ctx->start_time;
                    if (ctx->spawn_cnt < 3 || run_time > 3600) {
                        hproc_spawn(ctx);
                    }
                    else {
                        hloge("proc crash, pid=%d spawn_cnt=%d run_time=%us",
                                pid, ctx->spawn_cnt, (unsigned int)run_time);

                        bool have_worker = false;
                        for (int i = 0; i < g_main_ctx.worker_processes; ++i) {
                            if (g_main_ctx.proc_ctxs[i].pid > 0) {
                                have_worker = true;
                                break;
                            }
                        }
                        if (!have_worker) {
                            hlogw("No alive worker process, exit master process!");
                            exit(0);
                        }
                    }
                    break;
                }
            }
        }
    }
        break;
    case SIGNAL_RELOAD:
        if (g_main_ctx.reload_fn) {
            g_main_ctx.reload_fn(g_main_ctx.reload_userdata);
            if (getpid_from_pidfile() == getpid()) {
                // master send SIGNAL_RELOAD => workers
                for (int i = 0; i < g_main_ctx.worker_processes; ++i) {
                    if (g_main_ctx.proc_ctxs[i].pid <= 0) break;
                    kill(g_main_ctx.proc_ctxs[i].pid, SIGNAL_RELOAD);
                }
            }
        }
        break;
    default:
        break;
    }
}

int signal_init(procedure_t reload_fn, void* reload_userdata) {
    g_main_ctx.reload_fn = reload_fn;
    g_main_ctx.reload_userdata = reload_userdata;

    signal(SIGINT, signal_handler);
    signal(SIGCHLD, signal_handler);
    signal(SIGNAL_TERMINATE, signal_handler);
    signal(SIGNAL_RELOAD, signal_handler);

    return 0;
}

#elif defined(OS_WIN)
#include <mmsystem.h> // for timeSetEvent

// win32 use Event
//static HANDLE s_hEventTerm = NULL;
static HANDLE s_hEventReload = NULL;

static void WINAPI on_timer(UINT uTimerID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2) {
    DWORD ret;
    /*
    ret = WaitForSingleObject(s_hEventTerm, 0);
    if (ret == WAIT_OBJECT_0) {
        hlogi("pid=%d recv event [TERM]", getpid());
        if (getpid_from_pidfile() == getpid()) {
            timeKillEvent(uTimerID);
            exit(0);
        }
    }
    */

    ret = WaitForSingleObject(s_hEventReload, 0);
    if (ret == WAIT_OBJECT_0) {
        hlogi("pid=%d recv event [RELOAD]", getpid());
        if (g_main_ctx.reload_fn) {
            g_main_ctx.reload_fn(g_main_ctx.reload_userdata);
        }
    }
}

static void signal_cleanup(void) {
    //CloseHandle(s_hEventTerm);
    //s_hEventTerm = NULL;
    CloseHandle(s_hEventReload);
    s_hEventReload = NULL;
}

int signal_init(procedure_t reload_fn, void* reload_userdata) {
    g_main_ctx.reload_fn = reload_fn;
    g_main_ctx.reload_userdata = reload_userdata;

    char eventname[MAX_PATH] = {0};
    //snprintf(eventname, sizeof(eventname), "%s_term_event", g_main_ctx.program_name);
    //s_hEventTerm = CreateEvent(NULL, FALSE, FALSE, eventname);
    //s_hEventTerm = OpenEvent(EVENT_ALL_ACCESS, FALSE, eventname);
    snprintf(eventname, sizeof(eventname), "%s_reload_event", g_main_ctx.program_name);
    s_hEventReload = CreateEvent(NULL, FALSE, FALSE, eventname);

    timeSetEvent(1000, 1000, on_timer, 0, TIME_PERIODIC);

    atexit(signal_cleanup);
    return 0;
}
#endif

static void kill_proc(int pid) {
#ifdef OS_UNIX
    kill(pid, SIGNAL_TERMINATE);
#else
    //SetEvent(s_hEventTerm);
    //hv_sleep(1);
    HANDLE hproc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (hproc) {
        TerminateProcess(hproc, 0);
        CloseHandle(hproc);
    }
#endif
}

void signal_handle(const char* signal) {
    if (strcmp(signal, "start") == 0) {
        if (g_main_ctx.oldpid > 0) {
            printf("%s is already running, pid=%d\n", g_main_ctx.program_name, g_main_ctx.oldpid);
            exit(0);
        }
    } else if (strcmp(signal, "stop") == 0) {
        if (g_main_ctx.oldpid > 0) {
            kill_proc(g_main_ctx.oldpid);
            printf("%s stop/waiting\n", g_main_ctx.program_name);
        } else {
            printf("%s is already stopped\n", g_main_ctx.program_name);
        }
        exit(0);
    } else if (strcmp(signal, "restart") == 0) {
        if (g_main_ctx.oldpid > 0) {
            kill_proc(g_main_ctx.oldpid);
            printf("%s stop/waiting\n", g_main_ctx.program_name);
            hv_sleep(1);
        }
    } else if (strcmp(signal, "status") == 0) {
        if (g_main_ctx.oldpid > 0) {
            printf("%s start/running, pid=%d\n", g_main_ctx.program_name, g_main_ctx.oldpid);
        } else {
            printf("%s stop/waiting\n", g_main_ctx.program_name);
        }
        exit(0);
    } else if (strcmp(signal, "reload") == 0) {
        if (g_main_ctx.oldpid > 0) {
            printf("reload confile [%s]\n", g_main_ctx.confile);
#ifdef OS_UNIX
            kill(g_main_ctx.oldpid, SIGNAL_RELOAD);
#else
            SetEvent(s_hEventReload);
#endif
        }
        hv_sleep(1);
        exit(0);
    } else {
        printf("Invalid signal: '%s'\n", signal);
        exit(0);
    }
    printf("%s start/running\n", g_main_ctx.program_name);
}

// master-workers processes
static HTHREAD_ROUTINE(worker_thread) {
    hlogi("worker_thread pid=%ld tid=%ld", hv_getpid(), hv_gettid());
    if (g_main_ctx.worker_fn) {
        g_main_ctx.worker_fn(g_main_ctx.worker_userdata);
    }
    return 0;
}

static void worker_init(void* userdata) {
#ifdef OS_UNIX
    setproctitle("%s: worker process", g_main_ctx.program_name);
    signal(SIGNAL_RELOAD, signal_handler);
#endif
}

static void worker_proc(void* userdata) {
    for (int i = 1; i < g_main_ctx.worker_threads; ++i) {
        hthread_create(worker_thread, NULL);
    }
    worker_thread(NULL);
}

int master_workers_run(procedure_t worker_fn, void* worker_userdata,
        int worker_processes, int worker_threads, bool wait) {
#ifdef OS_WIN
        // NOTE: Windows not provide MultiProcesses
        if (worker_threads == 0) {
            // MultiProcesses => MultiThreads
            worker_threads = worker_processes;
        }
        worker_processes = 0;
#endif
    if (worker_threads == 0) worker_threads = 1;

    g_main_ctx.worker_threads = worker_threads;
    g_main_ctx.worker_fn = worker_fn;
    g_main_ctx.worker_userdata = worker_userdata;

    if (worker_processes == 0) {
        // single process
        if (wait) {
            for (int i = 1; i < worker_threads; ++i) {
                hthread_create(worker_thread, NULL);
            }
            worker_thread(NULL);
        }
        else {
            for (int i = 0; i < worker_threads; ++i) {
                hthread_create(worker_thread, NULL);
            }
        }
    }
    else {
        if (g_main_ctx.worker_processes != 0) {
            return ERR_OVER_LIMIT;
        }
        // master-workers processes
#ifdef OS_UNIX
        setproctitle("%s: master process", g_main_ctx.program_name);
        signal(SIGNAL_RELOAD, signal_handler);
#endif
        g_main_ctx.worker_processes = worker_processes;
        int bytes = g_main_ctx.worker_processes * sizeof(proc_ctx_t);
        SAFE_ALLOC(g_main_ctx.proc_ctxs, bytes);
        proc_ctx_t* ctx = g_main_ctx.proc_ctxs;
        for (int i = 0; i < g_main_ctx.worker_processes; ++i, ++ctx) {
            ctx->init = worker_init;
            ctx->proc = worker_proc;
            hproc_spawn(ctx);
            hlogi("workers[%d] start/running, pid=%d", i, ctx->pid);
        }
        g_main_ctx.pid = getpid();
        hlogi("master start/running, pid=%d", g_main_ctx.pid);
        if (wait) {
            while (1) hv_sleep (1);
        }
    }
    return 0;
}
