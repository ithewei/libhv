#ifndef HV_MAIN_H_
#define HV_MAIN_H_

#include "hplatform.h"
#include "hdef.h"
#include "hstring.h"
#include "hproc.h"

#ifdef _MSC_VER
#pragma comment(lib, "winmm.lib") // for timeSetEvent
#endif

typedef struct main_ctx_s {
    char    run_dir[MAX_PATH];
    char    program_name[MAX_PATH];
    char    confile[MAX_PATH]; // default etc/${program}.conf
    char    pidfile[MAX_PATH]; // default logs/${program}.pid
    char    logfile[MAX_PATH]; // default logs/${program}.log

    pid_t   pid;    // getpid
    pid_t   oldpid; // getpid_from_pidfile

    int     argc;
    int     arg_len;
    char**  os_argv;
    char**  save_argv;
    char*   cmdline;

    int     envc;
    int     env_len;
    char**  os_envp;
    char**  save_envp;

    keyval_t    arg_kv;
    StringList  arg_list;
    keyval_t    env_kv;
} main_ctx_t;

// arg_type
#define NO_ARGUMENT         0
#define REQUIRED_ARGUMENT   1
#define OPTIONAL_ARGUMENT   2
// option define
#define OPTION_PREFIX   '-'
#define OPTION_DELIM    '='
#define OPTION_ENABLE   "on"
#define OPTION_DISABLE  "off"
typedef struct option_s {
    char        short_opt;
    const char* long_opt;
    int         arg_type;
} option_t;

int main_ctx_init(int argc, char** argv);
// ls -a -l
// ls -al
// watch -n 10 ls
// watch -n10 ls
int parse_opt(int argc, char** argv, const char* opt);
// gcc -g -Wall -O3 -std=cpp main.c
int parse_opt_long(int argc, char** argv, const option_t* long_options, int size);
const char* get_arg(const char* key);
const char* get_env(const char* key);

#ifdef OS_UNIX
void setproctitle(const char* title);
#endif

// pidfile
int      create_pidfile();
void     delete_pidfile();
pid_t    getpid_from_pidfile();

// signal=[start,stop,restart,status,reload]
int signal_init(procedure_t reload_fn = NULL, void* reload_userdata = NULL);
void signal_handle(const char* signal);
#ifdef OS_UNIX
// we use SIGTERM to quit process, SIGUSR1 to reload confile
#define SIGNAL_TERMINATE    SIGTERM
#define SIGNAL_RELOAD       SIGUSR1
void signal_handler(int signo);
#endif

// global var
#define DEFAULT_WORKER_PROCESSES    4
#define MAXNUM_WORKER_PROCESSES     1024
extern main_ctx_t   g_main_ctx;
extern int          g_worker_processes_num;
extern int          g_worker_threads_num;
extern proc_ctx_t*  g_worker_processes;
extern procedure_t  g_worker_fn;
extern void*        g_worker_userdata;

// master-workers processes
int master_workers_run(procedure_t worker_fn, void* worker_userdata,
        int worker_processes = DEFAULT_WORKER_PROCESSES,
        int worker_threads = 0,
        bool wait = true);

#endif // HV_MAIN_H_
