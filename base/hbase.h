#ifndef HV_BASE_H_
#define HV_BASE_H_

#include "hdef.h"

BEGIN_EXTERN_C

//--------------------safe alloc/free---------------------------
extern unsigned int g_alloc_cnt;
extern unsigned int g_free_cnt;

void* safe_malloc(size_t size);
void* safe_realloc(void* oldptr, size_t newsize, size_t oldsize);
void* safe_calloc(size_t nmemb, size_t size);
void* safe_zalloc(size_t size);

#define HV_ALLOC(ptr, size)\
    do {\
        void** pptr = (void**)&(ptr);\
        *pptr = safe_zalloc(size);\
        printd("alloc(%p, size=%llu)\tat [%s:%d:%s]\n", ptr, (unsigned long long)size, __FILE__, __LINE__, __FUNCTION__);\
    } while(0)

#define HV_ALLOC_SIZEOF(ptr)  HV_ALLOC(ptr, sizeof(*(ptr)))

#define HV_FREE(ptr)\
    do {\
        if (ptr) {\
            printd("free( %p )\tat [%s:%d:%s]\n", ptr, __FILE__, __LINE__, __FUNCTION__);\
            free(ptr);\
            ptr = NULL;\
            ++g_free_cnt;\
        }\
    } while(0)

static inline void hv_memcheck() {
    printf("Memcheck => alloc:%u free:%u\n", g_alloc_cnt, g_free_cnt);
}

#define HV_MEMCHECK    atexit(hv_memcheck);

//--------------------safe string-------------------------------
char* strupper(char* str);
char* strlower(char* str);
char* strreverse(char* str);

bool strstartswith(const char* str, const char* start);
bool strendswith(const char* str, const char* end);
bool strcontains(const char* str, const char* sub);

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

#define strrchr_dot(str) strrchr(str, '.')
char* strrchr_dir(const char* filepath);

// basename
const char* hv_basename(const char* filepath);
const char* hv_suffixname(const char* filename);
// mkdir -p
int hv_mkdir_p(const char* dir);
// rmdir -p
int hv_rmdir_p(const char* dir);

// 1 y on yes true enable
bool getboolean(const char* str);

char* get_executable_path(char* buf, int size);
char* get_executable_dir(char* buf, int size);
char* get_executable_file(char* buf, int size);
char* get_run_dir(char* buf, int size);

END_EXTERN_C

#endif // HV_BASE_H_
