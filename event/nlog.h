#ifndef HV_NLOG_H_
#define HV_NLOG_H_

// nlog: extend hlog use hloop

/* you can recv log by:
 * Windows: telnet ip port
 * Linux: nc ip port
 */

/*
 * @see examples/hloop_test.c
#include "hlog.h"
#include "nlog.h"

void timer_write_log(htimer_t* timer) {
    static int cnt = 0;
    hlogi("[%d] Do you recv me?", ++cnt);
}

int main() {
    hloop_t* loop = hloop_new(0);
    hlog_set_logger(network_logger);
    nlog_listen(loop, DEFAULT_LOG_PORT);
    htimer_add(loop, timer_write_log, 1000, INFINITE);
    hloop_run(loop);
    hloop_free(&loop);
}
 */


#include "hexport.h"
#include "hloop.h"

#define DEFAULT_LOG_PORT    10514

BEGIN_EXTERN_C

HV_EXPORT void network_logger(int loglevel, const char* buf, int len);
HV_EXPORT hio_t* nlog_listen(hloop_t* loop, int port);

END_EXTERN_C

#endif // HV_NLOG_H_
