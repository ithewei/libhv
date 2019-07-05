#ifndef HTTPD_CONF_H_
#define HTTPD_CONF_H_

#include "h.h"
#include "iniparser.h"

typedef struct httpd_conf_ctx_s {
    IniParser* parser;
    int loglevel;
    int worker_processes;
    int port;
    int file_stat_interval;
    int file_cached_time;
} httpd_conf_ctx_t;

extern httpd_conf_ctx_t g_conf_ctx;

inline void conf_ctx_init(httpd_conf_ctx_t* ctx) {
    ctx->parser = new IniParser;
    ctx->loglevel = LOG_LEVEL_DEBUG;
    ctx->worker_processes = 0;
    ctx->port = 0;
    ctx->file_stat_interval = 10;
    ctx->file_cached_time   = 60;
}

#endif
