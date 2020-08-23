#ifndef HV_CONFIG_H_
#define HV_CONFIG_H_

#ifndef HAVE_STDBOOL_H
#define HAVE_STDBOOL_H 1
#endif

#ifndef HAVE_STDINT_H
#define HAVE_STDINT_H 1
#endif

#ifndef HAVE_STDATOMIC_H
#define HAVE_STDATOMIC_H 1
#endif

#ifndef HAVE_SYS_TYPES_H
#define HAVE_SYS_TYPES_H 1
#endif

#ifndef HAVE_SYS_STAT_H
#define HAVE_SYS_STAT_H 1
#endif

#ifndef HAVE_SYS_TIME_H
#define HAVE_SYS_TIME_H 1
#endif

#ifndef HAVE_FCNTL_H
#define HAVE_FCNTL_H 1
#endif

#ifndef HAVE_PTHREAD_H
#define HAVE_PTHREAD_H 1
#endif

#ifndef HAVE_GETTID
#define HAVE_GETTID 0
#endif

#ifndef HAVE_STRLCPY
#define HAVE_STRLCPY 1
#endif

#ifndef HAVE_STRLCAT
#define HAVE_STRLCAT 1
#endif

#ifndef HAVE_CLOCK_GETTIME
#define HAVE_CLOCK_GETTIME 1
#endif

#ifndef HAVE_GETTIMEOFDAY
#define HAVE_GETTIMEOFDAY 1
#endif

#ifndef HAVE_PTHREAD_SPIN_LOCK
#define HAVE_PTHREAD_SPIN_LOCK 0
#endif

#ifndef HAVE_PTHREAD_MUTEX_TIMEDLOCK
#define HAVE_PTHREAD_MUTEX_TIMEDLOCK 0
#endif

#ifndef HAVE_SEM_TIMEDWAIT
#define HAVE_SEM_TIMEDWAIT 0
#endif

#endif // HV_CONFIG_H_
