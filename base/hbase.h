#ifndef HV_BASE_H_
#define HV_BASE_H_

/*
 * @功能：此头文件实现了一些常用的函数
 *
 */

#include "hexport.h"
#include "hplatform.h" // for bool
#include "hdef.h" // for printd

BEGIN_EXTERN_C

// 安全的内存分配与释放函数
//--------------------safe alloc/free---------------------------
HV_EXPORT void* safe_malloc(size_t size);
HV_EXPORT void* safe_realloc(void* oldptr, size_t newsize, size_t oldsize);
HV_EXPORT void* safe_calloc(size_t nmemb, size_t size);
HV_EXPORT void* safe_zalloc(size_t size);
HV_EXPORT void  safe_free(void* ptr);

// 分配内存宏
#define HV_ALLOC(ptr, size)\
    do {\
        *(void**)&(ptr) = safe_zalloc(size);\
        printd("alloc(%p, size=%llu)\tat [%s:%d:%s]\n", ptr, (unsigned long long)size, __FILE__, __LINE__, __FUNCTION__);\
    } while(0)

// 通过sizeof计算内存占用大小，并分配内存，通常用于给结构体分配内存
#define HV_ALLOC_SIZEOF(ptr)  HV_ALLOC(ptr, sizeof(*(ptr)))

// 释放内存宏
#define HV_FREE(ptr)\
    do {\
        if (ptr) {\
            safe_free(ptr);\
            printd("free( %p )\tat [%s:%d:%s]\n", ptr, __FILE__, __LINE__, __FUNCTION__);\
            ptr = NULL;\
        }\
    } while(0)

// 统计内存分配/释放的次数，以此检查是否有内存未释放
HV_EXPORT long hv_alloc_cnt();
HV_EXPORT long hv_free_cnt();
HV_INLINE void hv_memcheck() {
    printf("Memcheck => alloc:%ld free:%ld\n", hv_alloc_cnt(), hv_free_cnt());
}
#define HV_MEMCHECK    atexit(hv_memcheck);

// 一些字符串操作函数
//--------------------safe string-------------------------------
// 字符串转大写
HV_EXPORT char* strupper(char* str);
// 字符串转小写
HV_EXPORT char* strlower(char* str);
// 字符串翻转
HV_EXPORT char* strreverse(char* str);

// 判断字符串是否以某个字符串开头
HV_EXPORT bool strstartswith(const char* str, const char* start);
// 判断字符串是否以某个字符串结尾
HV_EXPORT bool strendswith(const char* str, const char* end);
// 判断字符串中是否包含某个子串
HV_EXPORT bool strcontains(const char* str, const char* sub);

// 标准库里的strncpy、strncat通常需要传入sizeof(dest_buf)-1，
// 如果不小心传成sizeof(dest_buf)，可能造成内存越界访问以及不是以'\0'结尾，
// 所以这里重新实现了下，传入sizeof(dest_buf)也能安全工作
// strncpy n = sizeof(dest_buf)-1
// safe_strncpy n = sizeof(dest_buf)
HV_EXPORT char* safe_strncpy(char* dest, const char* src, size_t n);

// strncat n = sizeof(dest_buf)-1-strlen(dest)
// safe_strncpy n = sizeof(dest_buf)
HV_EXPORT char* safe_strncat(char* dest, const char* src, size_t n);

// 某些系统库里没有定义strlcpy和strlcat，这里兼容处理下
#if !HAVE_STRLCPY
#define strlcpy safe_strncpy
#endif

#if !HAVE_STRLCAT
#define strlcat safe_strncat
#endif

// 查找最后一个点符号，通常用于分离文件名和后缀
#define strrchr_dot(str) strrchr(str, '.')
// 查找最后一个路径符号，通常用于分离路径和文件名
HV_EXPORT char* strrchr_dir(const char* filepath);

// basename
// 获取文件名，使用上面的strrchr_dir实现
HV_EXPORT const char* hv_basename(const char* filepath);
// 获取后缀名，使用上面的strrchr_dot实现
HV_EXPORT const char* hv_suffixname(const char* filename);
// mkdir -p
// 递归创建文件夹
HV_EXPORT int hv_mkdir_p(const char* dir);
// rmdir -p
// 递归删除文件夹
HV_EXPORT int hv_rmdir_p(const char* dir);

// 1 y on yes true enable
// 根据字符串返回对应表示的布尔类型值
HV_EXPORT bool getboolean(const char* str);

// 获取可执行文件完整路径
HV_EXPORT char* get_executable_path(char* buf, int size);
// 获取可执行文件所在目录
HV_EXPORT char* get_executable_dir(char* buf, int size);
// 获取可执行文件名
HV_EXPORT char* get_executable_file(char* buf, int size);
// 获取运行目录
HV_EXPORT char* get_run_dir(char* buf, int size);

END_EXTERN_C

#endif // HV_BASE_H_
