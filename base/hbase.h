#ifndef HW_BASE_H_
#define HW_BASE_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hdef.h"

#ifdef __cplusplus
extern "C" {
#endif

//---------------------safe alloc/free---------------------------
extern unsigned int g_alloc_cnt;
extern unsigned int g_free_cnt;

void* safe_malloc(size_t size);
void* safe_realloc(void* oldptr, size_t newsize, size_t oldsize);
void* safe_calloc(size_t nmemb, size_t size);
void* safe_zalloc(size_t size);

#undef  SAFE_ALLOC
#define SAFE_ALLOC(ptr, size)\
    do {\
        void** pptr = (void**)&(ptr);\
        *pptr = safe_zalloc(size);\
        printd("alloc(%p, size=%lu)\tat [%s:%d:%s]\n", ptr, size, __FILE__, __LINE__, __FUNCTION__);\
    } while(0)

#define SAFE_ALLOC_SIZEOF(ptr)  SAFE_ALLOC(ptr, sizeof(*(ptr)))

#undef  SAFE_FREE
#define SAFE_FREE(ptr)\
    do {\
        if (ptr) {\
            printd("free( %p )\tat [%s:%d:%s]\n", ptr, __FILE__, __LINE__, __FUNCTION__);\
            free(ptr);\
            ptr = NULL;\
            ++g_free_cnt;\
        }\
    } while(0)

static inline void memcheck() {
    printf("Memcheck => alloc:%u free:%u\n", g_alloc_cnt, g_free_cnt);
}

#define MEMCHECK    atexit(memcheck);

//-----------------------------safe string-----------------------
char* strupper(char* str);
char* strlower(char* str);
char* strreverse(char* str);

// strncpy n = sizeof(dest_buf)-1
// safe_strncpy n = sizeof(dest_buf)
char* safe_strncpy(char* dest, const char* src, size_t n);

// strncat n = sizeof(dest_buf)-1-strlen(dest)
// safe_strncpy n = sizeof(dest_buf)
char* safe_strncat(char* dest, const char* src, size_t n);

#if !HAVE_STRLCPY
#define strlcpy safe_strncpy
#endif

#if !HAVE_STRLCAT
#define strlcat safe_strncat
#endif

// 1 y on yes true enable
bool getboolean(const char* str);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // HW_BASE_H_
