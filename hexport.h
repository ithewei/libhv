#ifndef HV_EXPORT_H_
#define HV_EXPORT_H_

/*
 * @功能: 此头文件主要是定义动态库导出宏HV_EXPORT
 *
 * @备注: 如果是以静态库方式使用，需将HV_STATICLIB加入到预编译宏
 *
 */

// HV_EXPORT
// 接口导出宏
#if defined(HV_STATICLIB) || defined(HV_SOURCE)
    #define HV_EXPORT
#elif defined(_MSC_VER)
    #if defined(HV_DYNAMICLIB) || defined(HV_EXPORTS) || defined(hv_EXPORTS)
        #define HV_EXPORT  __declspec(dllexport)
    #else
        #define HV_EXPORT  __declspec(dllimport)
    #endif
#elif defined(__GNUC__)
    #define HV_EXPORT  __attribute__((visibility("default")))
#else
    #define HV_EXPORT
#endif

// HV_INLINE
#define HV_INLINE static inline

// HV_DEPRECATED
// 接口过时声明宏
#if defined(HV_NO_DEPRECATED)
#define HV_DEPRECATED
#elif defined(__GNUC__) || defined(__clang__)
#define HV_DEPRECATED   __attribute__((deprecated))
#elif defined(_MSC_VER)
#define HV_DEPRECATED   __declspec(deprecated)
#else
#define HV_DEPRECATED
#endif

// HV_UNUSED
// 参数未使用宏
#if defined(__GNUC__)
    #define HV_UNUSED   __attribute__((visibility("unused")))
#else
    #define HV_UNUSED
#endif

// @param[IN | OUT | INOUT]
/*
 * 参数描述宏：
 * IN   => 输入参数
 * OUT  => 输出参数
 * INOUT=> 既作输入参数，又作输出参数
 *
 */
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
/*
 * 字段描述宏：
 * OPTIONAL => 可选字段
 * REQUIRED => 必需字段
 * REPEATED => 可重复字段
 *
 */
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

/*
 * @NOTE：extern "C"的作用
 * 由于C++支持函数重载，而C语言不支持，因此函数被C++编译后在符号库中的名字是与C语言不同的；
 * C++编译后的函数需要加上参数的类型才能唯一标定重载后的函数，
 * 加上extern "C"，是为了向编译器指明这段代码按照C语言的方式进行编译。
 *
 */
#ifndef EXTERN_C
#define EXTERN_C            extern "C"
#endif

#ifndef BEGIN_EXTERN_C
#define BEGIN_EXTERN_C      extern "C" {
#endif

#ifndef END_EXTERN_C
#define END_EXTERN_C        } // extern "C"
#endif

// 命名空间声明宏
#ifndef BEGIN_NAMESPACE
#define BEGIN_NAMESPACE(ns) namespace ns {
#endif

#ifndef END_NAMESPACE
#define END_NAMESPACE(ns)   } // namespace ns
#endif

#ifndef USING_NAMESPACE
#define USING_NAMESPACE(ns) using namespace ns;
#endif

// 缺省值
#ifndef DEFAULT
#define DEFAULT(x)  = x
#endif

// 枚举类型声明
#ifndef ENUM
#define ENUM(e)     enum e
#endif

// 结构体类型声明
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

// MSVC ports
#ifdef _MSC_VER
#if _MSC_VER < 1900 // < VS2015

#ifndef __cplusplus
#ifndef inline
#define inline __inline
#endif
#endif

#ifndef snprintf
#define snprintf _snprintf
#endif

#endif
#endif

#endif // HV_EXPORT_H_
