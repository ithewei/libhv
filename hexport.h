#ifndef HV_EXPORT_H_
#define HV_EXPORT_H_

// HV_EXPORT
#ifdef HV_STATICLIB
    #define HV_EXPORT
#elif defined(_WIN32)
    #if defined(HV_EXPORTS) || defined(hv_EXPORTS)
        #define HV_EXPORT  __declspec(dllexport)
    #else
        #define HV_EXPORT  __declspec(dllimport)
    #endif
#elif defined(__GNUC__)
    #define HV_EXPORT  __attribute__((visibility("default")))
#else
    #define HV_EXPORT
#endif

// DEPRECATED
#if defined(__GNUC__) || defined(__clang__)
    #define DEPRECATED __attribute__((visibility("deprecated")))
#else
    #define DEPRECATED
#endif

// @param[IN | OUT | INOUT]
#ifndef IN
#define IN
#endif

#ifndef OUT
#define OUT
#endif

#ifndef INOUT
#define INOUT
#endif

// @field[OPTIONAL | REQUIRED | REPEATED]
#ifndef OPTIONAL
#define OPTIONAL
#endif

#ifndef REQUIRED
#define REQUIRED
#endif

#ifndef REPEATED
#define REPEATED
#endif

#ifdef __cplusplus

#ifndef EXTERN_C
#define EXTERN_C            extern "C"
#endif

#ifndef BEGIN_EXTERN_C
#define BEGIN_EXTERN_C      extern "C" {
#endif

#ifndef END_EXTERN_C
#define END_EXTERN_C        } // extern "C"
#endif

#ifndef BEGIN_NAMESPACE
#define BEGIN_NAMESPACE(ns) namespace ns {
#endif

#ifndef END_NAMESPACE
#define END_NAMESPACE(ns)   } // namespace ns
#endif

#ifndef USING_NAMESPACE
#define USING_NAMESPACE(ns) using namespace ns;
#endif

#ifndef DEFAULT
#define DEFAULT(x)  = x
#endif

#ifndef ENUM
#define ENUM(e)     enum e
#endif

#ifndef STRUCT
#define STRUCT(s)   struct s
#endif

#else

#define EXTERN_C    extern
#define BEGIN_EXTERN_C
#define END_EXTERN_C

#define BEGIN_NAMESPACE(ns)
#define END_NAMESPACE(ns)
#define USING_NAMESPACE(ns)

#ifndef DEFAULT
#define DEFAULT(x)
#endif

#ifndef ENUM
#define ENUM(e)\
typedef enum e e;\
enum e
#endif

#ifndef STRUCT
#define STRUCT(s)\
typedef struct s s;\
struct s
#endif

#endif // __cplusplus

#define BEGIN_NAMESPACE_HV  BEGIN_NAMESPACE(hv)
#define END_NAMESPACE_HV    END_NAMESPACE(hv)
#define USING_NAMESPACE_HV  USING_NAMESPACE(hv)

#endif // HV_EXPORT_H_
