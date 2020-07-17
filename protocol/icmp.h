#ifndef HV_ICMP_H_
#define HV_ICMP_H_

#include "hexport.h"

BEGIN_EXTERN_C

// @param cnt: ping count
// @return: ok count
// @note: printd $CC -DPRINT_DEBUG
HV_EXPORT int ping(const char* host, int cnt DEFAULT(4));

END_EXTERN_C

#endif // HV_ICMP_H_
