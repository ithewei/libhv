#ifndef HV_ATOMIC_H_
#define HV_ATOMIC_H_

#ifdef __cplusplus

// c++11
#include <atomic>

using std::atomic_flag;
using std::atomic_long;

#define ATOMIC_FLAG_TEST_AND_SET(p)     ((p)->test_and_set())
#define ATOMIC_FLAG_CLEAR(p)            ((p)->clear())

#else

#include "hplatform.h"  // for HAVE_STDATOMIC_H
#if HAVE_STDATOMIC_H

// c11
#include <stdatomic.h>

#define ATOMIC_FLAG_TEST_AND_SET    atomic_flag_test_and_set
#define ATOMIC_FLAG_CLEAR           atomic_flag_clear
#define ATOMIC_ADD                  atomic_fetch_add
#define ATOMIC_SUB                  atomic_fetch_sub
#define ATOMIC_INC(p)               ATOMIC_ADD(p, 1)
#define ATOMIC_DEC(p)               ATOMIC_SUB(p, 1)

#else

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

#ifdef _WIN32

#define ATOMIC_FLAG_TEST_AND_SET    atomic_flag_test_and_set
static inline bool atomic_flag_test_and_set(atomic_flag* p) {
    // return InterlockedIncrement((LONG*)&p->_Value, 1);
    return InterlockedCompareExchange((LONG*)&p->_Value, 1, 0);
}

#define ATOMIC_ADD          InterlockedAdd
#define ATOMIC_SUB(p, n)    InterlockedAdd(p, -n)
#define ATOMIC_INC          InterlockedIncrement
#define ATOMIC_DEC          InterlockedDecrement

#elif defined(__GNUC__)

#define ATOMIC_FLAG_TEST_AND_SET    atomic_flag_test_and_set
static inline bool atomic_flag_test_and_set(atomic_flag* p) {
    return !__sync_bool_compare_and_swap(&p->_Value, 0, 1);
}

#define ATOMIC_ADD          __sync_fetch_and_add
#define ATOMIC_SUB          __sync_fetch_and_sub
#define ATOMIC_INC(p)       ATOMIC_ADD(p, 1)
#define ATOMIC_DEC(p)       ATOMIC_SUB(p, 1)

#endif

#endif // HAVE_STDATOMIC_H

#endif // __cplusplus

#ifndef ATOMIC_FLAG_INIT
#define ATOMIC_FLAG_INIT        { 0 }
#endif

#ifndef ATOMIC_VAR_INIT
#define ATOMIC_VAR_INIT(value)  (value)
#endif

#ifndef ATOMIC_FLAG_TEST_AND_SET
#define ATOMIC_FLAG_TEST_AND_SET    atomic_flag_test_and_set
static inline bool atomic_flag_test_and_set(atomic_flag* p) {
    bool ret = p->_Value;
    p->_Value = 1;
    return ret;
}
#endif

#ifndef ATOMIC_FLAG_CLEAR
#define ATOMIC_FLAG_CLEAR           atomic_flag_clear
static inline void atomic_flag_clear(atomic_flag* p) {
    p->_Value = 0;
}
#endif

#ifndef ATOMIC_ADD
#define ATOMIC_ADD(p, n)    (*(p) += (n))
#endif

#ifndef ATOMIC_SUB
#define ATOMIC_SUB(p, n)    (*(p) -= (n))
#endif

#ifndef ATOMIC_INC
#define ATOMIC_INC(p)       ((*(p))++)
#endif

#ifndef ATOMIC_DEC
#define ATOMIC_DEC(p)       ((*(p))--)
#endif

typedef atomic_flag                 hatomic_flag_t;
#define HATOMIC_FLAG_INIT           ATOMIC_FLAG_INIT
#define hatomic_flag_test_and_set   ATOMIC_FLAG_TEST_AND_SET
#define hatomic_flag_clear          ATOMIC_FLAG_CLEAR

typedef atomic_long                 hatomic_t;
#define HATOMIC_VAR_INIT            ATOMIC_VAR_INIT
#define hatomic_add                 ATOMIC_ADD
#define hatomic_sub                 ATOMIC_SUB
#define hatomic_inc                 ATOMIC_INC
#define hatomic_dec                 ATOMIC_DEC

#endif // HV_ATOMIC_H_
