#include "hbase.h"

#ifdef OS_DARWIN
#include <mach-o/dyld.h> // for _NSGetExecutablePath
#endif

#include "hatomic.h"

static hatomic_t s_alloc_cnt = HATOMIC_VAR_INIT(0);
static hatomic_t s_free_cnt = HATOMIC_VAR_INIT(0);

long hv_alloc_cnt() {
    return s_alloc_cnt;
}

long hv_free_cnt() {
    return s_free_cnt;
}

void* safe_malloc(size_t size) {
    // 原子增加内存分配计数
    hatomic_inc(&s_alloc_cnt);
    void* ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "malloc failed!\n");
        exit(-1);
    }
    return ptr;
}

void* safe_realloc(void* oldptr, size_t newsize, size_t oldsize) {
    // 原子增加内存分配计数
    hatomic_inc(&s_alloc_cnt);
    // 原子增加内存释放计数
    hatomic_inc(&s_free_cnt);
    void* ptr = realloc(oldptr, newsize);
    if (!ptr) {
        fprintf(stderr, "realloc failed!\n");
        exit(-1);
    }
    // 将新增的内存置0（系统库里的realloc未置空）
    if (newsize > oldsize) {
        memset((char*)ptr + oldsize, 0, newsize - oldsize);
    }
    return ptr;
}

void* safe_calloc(size_t nmemb, size_t size) {
    hatomic_inc(&s_alloc_cnt);
    void* ptr =  calloc(nmemb, size);
    if (!ptr) {
        fprintf(stderr, "calloc failed!\n");
        exit(-1);
    }
    return ptr;
}

void* safe_zalloc(size_t size) {
    hatomic_inc(&s_alloc_cnt);
    void* ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "malloc failed!\n");
        exit(-1);
    }
    memset(ptr, 0, size);
    return ptr;
}

void safe_free(void* ptr) {
    if (ptr) {
        free(ptr);
        ptr = NULL;
        hatomic_inc(&s_free_cnt);
    }
}

char* strupper(char* str) {
    char* p = str;
    while (*p != '\0') {
        if (*p >= 'a' && *p <= 'z') {
            // 大小写只有bits[5]不同，利用这一特性，使用位操作符比加减操作更高效
            *p &= ~0x20;
        }
        ++p;
    }
    return str;
}

char* strlower(char* str) {
    char* p = str;
    while (*p != '\0') {
        if (*p >= 'A' && *p <= 'Z') {
            // 大小写只有bits[5]不同，利用这一特性，使用位操作符比加减操作更高效
            *p |= 0x20;
        }
        ++p;
    }
    return str;
}

char* strreverse(char* str) {
    if (str == NULL) return NULL;
    char* b = str;
    char* e = str;
    while(*e) {++e;} // 此时e指向‘\0’结束符
    --e; // 此时e指向最后一个字符
    char tmp;
    // 头尾指针法
    while (e > b) {
        tmp = *e;
        *e = *b;
        *b = tmp;
        --e;
        ++b;
    }
    return str;
}

// n = sizeof(dest_buf)
char* safe_strncpy(char* dest, const char* src, size_t n) {
    assert(dest != NULL && src != NULL);
    char* ret = dest;
    while (*src != '\0' && --n > 0) {
        *dest++ = *src++;
    }
    *dest = '\0';
    return ret;
}

// n = sizeof(dest_buf)
char* safe_strncat(char* dest, const char* src, size_t n) {
    assert(dest != NULL && src != NULL);
    char* ret = dest;
    while (*dest) {++dest;--n;}
    while (*src != '\0' && --n > 0) {
        *dest++ = *src++;
    }
    *dest = '\0';
    return ret;
}

bool strstartswith(const char* str, const char* start) {
    assert(str != NULL && start != NULL);
    while (*str && *start && *str == *start) {
        ++str;
        ++start;
    }
    // 如果start走到了'\0'结束符，说明str是以start开头
    return *start == '\0';
}

bool strendswith(const char* str, const char* end) {
    assert(str != NULL && end != NULL);
    int len1 = 0;
    int len2 = 0;
    while (*str) {++str; ++len1;} // 统计str的长度，str此时走到了'\0'结束符
    while (*end) {++end; ++len2;} // 统计end的长度，end此时走到了'\0'结束符
    // 如果str的长度比end的长度还小，当然不可能是以end字符串结尾，直接返回false
    if (len1 < len2) return false;
    // 从最后一个字符开始比较是否相等
    while (len2-- > 0) {
        --str;
        --end;
        if (*str != *end) {
            return false;
        }
    }
    return true;
}

bool strcontains(const char* str, const char* sub) {
    assert(str != NULL && sub != NULL);
    // 直接使用了标准库函数strstr
    return strstr(str, sub) != NULL;
}

char* strrchr_dir(const char* filepath) {
    char* p = (char*)filepath;
    while (*p) ++p;
    while (--p >= filepath) {
#ifdef OS_WIN
        // windows下通常以正斜杠‘\’表示路径分隔符
        if (*p == '/' || *p == '\\')
#else
        // unix下以反斜杠‘/’表示路径分隔符
        if (*p == '/')
#endif
            return p;
    }
    return NULL;
}

const char* hv_basename(const char* filepath) {
    // 找到最后一个路径分割符，返回pos+1
    const char* pos = strrchr_dir(filepath);
    return pos ? pos+1 : filepath;
}

const char* hv_suffixname(const char* filename) {
    // 找到最后一个点符号，返回pos+1
    const char* pos = strrchr_dot(filename);
    return pos ? pos+1 : "";
}

int hv_mkdir_p(const char* dir) {
    // 如果路径可访问，说明路径已经存在，直接返回
    if (access(dir, 0) == 0) {
        return EEXIST;
    }
    char tmp[MAX_PATH];
    safe_strncpy(tmp, dir, sizeof(tmp));
    char* p = tmp;
    char delim = '/';
    // 通过路径分隔符，一级一级的创建子目录
    while (*p) {
#ifdef OS_WIN
        if (*p == '/' || *p == '\\') {
            delim = *p;
#else
        if (*p == '/') {
#endif
            *p = '\0';
            hv_mkdir(tmp);
            *p = delim;
        }
        ++p;
    }
    // 创建最后一级目录
    if (hv_mkdir(tmp) != 0) {
        return EPERM;
    }
    return 0;
}

int hv_rmdir_p(const char* dir) {
    // 如果路径不可访问，说明路径不存在，直接返回
    if (access(dir, 0) != 0) {
        return ENOENT;
    }
    // 删除最后一级目录
    if (rmdir(dir) != 0) {
        return EPERM;
    }
    char tmp[MAX_PATH];
    safe_strncpy(tmp, dir, sizeof(tmp));
    char* p = tmp;
    while (*p) ++p;
    // 通过路径分隔符，一级一级的删除父目录
    while (--p >= tmp) {
#ifdef OS_WIN
        if (*p == '/' || *p == '\\') {
#else
        if (*p == '/') {
#endif
            *p = '\0';
            if (rmdir(tmp) != 0) {
                return 0;
            }
        }
    }
    return 0;
}

bool getboolean(const char* str) {
    if (str == NULL) return false;
    int len = strlen(str);
    if (len == 0) return false;
    // 通过长度判断后再和对应字符串做比较，
    // 而不需要和每个代表true的字符串都对比一遍。
    switch (len) {
    case 1: return *str == '1' || *str == 'y' || *str == 'Y';
    case 2: return stricmp(str, "on") == 0;
    case 3: return stricmp(str, "yes") == 0;
    case 4: return stricmp(str, "true") == 0;
    case 6: return stricmp(str, "enable") == 0;
    default: return false;
    }
}

// 获取可执行文件路径在各个操作系统上的实现不同，这里提供统一的封装接口
char* get_executable_path(char* buf, int size) {
#ifdef OS_WIN
    GetModuleFileName(NULL, buf, size);
#elif defined(OS_LINUX)
    if (readlink("/proc/self/exe", buf, size) == -1) {
        return NULL;
    }
#elif defined(OS_DARWIN)
    _NSGetExecutablePath(buf, (uint32_t*)&size);
#endif
    return buf;
}

char* get_executable_dir(char* buf, int size) {
    char filepath[MAX_PATH];
    get_executable_path(filepath, sizeof(filepath));
    char* pos = strrchr_dir(filepath);
    if (pos) {
        *pos = '\0';
        strncpy(buf, filepath, size);
    }
    return buf;
}

char* get_executable_file(char* buf, int size) {
    char filepath[MAX_PATH];
    get_executable_path(filepath, sizeof(filepath));
    char* pos = strrchr_dir(filepath);
    if (pos) {
        strncpy(buf, pos+1, size);
    }
    return buf;
}

char* get_run_dir(char* buf, int size) {
    return getcwd(buf, size);
}
