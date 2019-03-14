#ifndef H_MAIN_H_
#define H_MAIN_H_

#include "hplatform.h"
#include "hdef.h"
#include "hstring.h"

typedef struct main_ctx_s {
    pid_t   oldpid; // getpid_from_pidfile
    pid_t   pid;    // getpid
    char    run_path[MAX_PATH];
    char    program_name[MAX_PATH];

    int     argc;
    int     arg_len;
    char**  os_argv;
    char**  save_argv;
    char*   cmdline;

    int     envc;
    int     env_len;
    char**  os_envp;
    char**  save_envp;

    char    confile[MAX_PATH]; // default etc/${program}.conf
    char    pidfile[MAX_PATH]; // default logs/${program}.pid
    char    logfile[MAX_PATH]; // default logs/${program}.log

    keyval_t    arg_kv;
    StringList  arg_list;
    keyval_t    env_kv;

    void*   confile_parser; // deprecated
} main_ctx_t;

extern main_ctx_t g_main_ctx;

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

int      create_pidfile();
void     delete_pidfile();
pid_t    getpid_from_pidfile();

#endif // H_MAIN_H_
