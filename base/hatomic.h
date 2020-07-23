#ifndef HV_ATOMIC_H_
#define HV_ATOMIC_H_

#ifdef __cplusplus

// c++11
#include <atomic>

#else

#include "hconfig.h"    // for HAVE_STDATOMIC_H
#if HAVE_STDATOMIC_H

// c11
#include <stdatomic.h>

#else

#include "hplatform.h"  // for bool, size_t

typedef volatile bool               atomic_bool;
typedef volatile char               atomic_char;
typedef volatile unsigned char      atomic_uchar;
typedef volatile short              atomic_short;
typedef volatile unsigned short     atomic_ushort;
typedef volatile int                atomic_int;
typedef volatile unsigned int       atomic_uint;
typedef volatile long               atomic_long;
typedef volatile unsigned long      atomic_ulong;
typedef volatile long long          atomic_llong;
typedef volatile unsigned long long atomic_ullong;
typedef volatile size_t             atomic_size_t;

typedef struct atomic_flag { atomic_bool _Value; } atomic_flag;

#define ATOMIC_FLAG_INIT        { 0 }
#define ATOMIC_VAR_INIT(value)  (value)

#ifdef __GNUC__

static inline bool atomic_flag_test_and_set(atomic_flag* p) {
    return !__sync_bool_compare_and_swap(&p->_Value, 0, 1);
}

static inline void atomic_flag_clear(atomic_flag* p) {
    p->_Value = 0;
}

#define atomic_fetch_add    __sync_fetch_and_add
#define atomic_fetch_sub    __sync_fetch_add_sub

#else

static inline bool atomic_flag_test_and_set(atomic_flag* p) {
    bool ret = p->_Value;
    p->_Value = 1;
    return ret;
}

static inline void atomic_flag_clear(atomic_flag* p) {
    p->_Value = 0;
}

#define atomic_fetch_add(p, n)  *(p); *(p) += (n)
#define atomic_fetch_sub(p, n)  *(p); *(p) -= (n)

#endif // __GNUC__
#endif // HAVE_STDATOMIC_H

#ifndef ATOMIC_ADD
#define ATOMIC_ADD      atomic_fetch_add
#endif

#ifndef ATOMIC_SUB
#define ATOMIC_SUB      atomic_fetch_sub
#endif

#ifndef ATOMIC_INC
#define ATOMIC_INC(p)   ATOMIC_ADD(p, 1)
#endif

#ifndef ATOMIC_DEC
#define ATOMIC_DEC(p)   ATOMIC_SUB(p, 1)
#endif

#endif // __cplusplus

#endif // HV_ATOMIC_H_
