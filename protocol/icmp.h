#ifndef HW_ICMP_H_
#define HW_ICMP_H_

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

#endif // HW_ICMP_H_
