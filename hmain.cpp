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

