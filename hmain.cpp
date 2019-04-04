#include "hmain.h"

#include <signal.h> // for kill
#include <errno.h>

#include "hplatform.h"
#include "hlog.h"

main_ctx_t g_main_ctx;

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
#ifdef OS_WIN
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
    char* cmdline = (char*)malloc(g_main_ctx.arg_len);
    g_main_ctx.cmdline = cmdline;
    for (i = 0; argv[i]; ++i) {
        g_main_ctx.save_argv[i] = argp;
        strcpy(g_main_ctx.save_argv[i], argv[i]);
        argp += strlen(argv[i]) + 1;

        strcpy(cmdline, argv[i]);
        cmdline += strlen(argv[i]);
        *cmdline = ' ';
        ++cmdline;
    }
    g_main_ctx.save_argv[g_main_ctx.argc] = NULL;
    g_main_ctx.cmdline[g_main_ctx.arg_len-1] = '\0';

#if defined(OS_WIN) || defined(OS_LINUX)
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
#endif

    char logpath[MAX_PATH] = {0};
    snprintf(logpath, sizeof(logpath), "%s/logs", g_main_ctx.run_path);
    MKDIR(logpath);
    snprintf(g_main_ctx.confile, sizeof(g_main_ctx.confile), "%s/etc/%s.conf", g_main_ctx.run_path, g_main_ctx.program_name);
    snprintf(g_main_ctx.pidfile, sizeof(g_main_ctx.pidfile), "%s/logs/%s.pid", g_main_ctx.run_path, g_main_ctx.program_name);
    snprintf(g_main_ctx.logfile, sizeof(g_main_ctx.confile), "%s/logs/%s.log", g_main_ctx.run_path, g_main_ctx.program_name);

    g_main_ctx.oldpid = getpid_from_pidfile();
#ifdef OS_UNIX
    if (kill(g_main_ctx.oldpid, 0) == -1 && errno == ESRCH) {
        g_main_ctx.oldpid = -1;
    }
#endif

    return 0;
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
    for (int i = 1; argv[i]; ++i) {
        char* p = argv[i];
        if (*p != '-') {
            g_main_ctx.arg_list.push_back(argv[i]);
            continue;
        }
        while (*++p) {
            int arg_type = get_arg_type(*p, options);
            if (arg_type == UNDEFINED_OPTION) {
                printf("Invalid option '%c'\n", *p);
                return -20;
            } else if (arg_type == NO_ARGUMENT) {
                g_main_ctx.arg_kv[std::string(p, 1)] = OPTION_ENABLE;
                continue;
            } else if (arg_type == REQUIRED_ARGUMENT) {
                if (*(p+1) != '\0') {
                    g_main_ctx.arg_kv[std::string(p, 1)] = p+1;
                    break;
                } else if (argv[i+1] != NULL) {
                    g_main_ctx.arg_kv[std::string(p, 1)] = argv[++i];
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
        if (delim == arg || delim == arg+arg_len-1 || delim-arg > MAX_OPTION) {
            printf("Invalid option '%s'\n", argv[i]);
            return -10;
        }
        if (delim) {
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
                g_main_ctx.arg_list.push_back(arg);
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
            g_main_ctx.arg_kv[std::string(1, pOption->short_opt)] = value;
        } else if (pOption->long_opt) {
            g_main_ctx.arg_kv[pOption->long_opt] = value;
        }
    }
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

#ifdef OS_UNIX
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
        //printf("fread [%s] bytes=%d\n", g_main_ctx.pidfile, readbytes);
        return -1;
    }
    return atoi(pid);
}

