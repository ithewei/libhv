#ifndef HV_ICMP_H_
#define HV_ICMP_H_

#include "hdef.h"

#ifdef __cplusplus
extern "C" {
#endif

// @param cnt: ping count
// @return: ok count
// @note: printd $CC -DPRINT_DEBUG
int ping(const char* host, int cnt DEFAULT(4));

#ifdef __cplusplus
}
#endif

#endif // HV_ICMP_H_
