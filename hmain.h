#ifndef H_MAIN_H_
#define H_MAIN_H_

#include "hplatform.h"
#include "hdef.h"

typedef struct main_ctx_s {
    pid_t   oldpid; // getpid_from_pidfile
    pid_t   pid;    // getpid
    char    run_path[MAX_PATH];
    char    program_name[MAX_PATH];

    int     argc;
    int     arg_len;
    char**  os_argv;
    char**  save_argv;

    int     envc;
    int     env_len;
    char**  os_envp;
    char**  save_envp;


    char    confile[MAX_PATH]; // default etc/${program}.conf
    char    pidfile[MAX_PATH]; // default logs/${program}.pid
    char    logfile[MAX_PATH]; // default logs/${program}.log

    keyval_t    arg_kv;
    keyval_t    env_kv;

    void*   confile_parser; // deprecated
} main_ctx_t;

extern main_ctx_t g_main_ctx;

int main_ctx_init(int argc, char** argv);
const char* get_arg(const char* key);
const char* get_env(const char* key);

#ifdef __unix__
void setproctitle(const char* title);
#endif

#endif // H_MAIN_H_
