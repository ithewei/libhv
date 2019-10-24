#include "hbase.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

unsigned int g_alloc_cnt = 0;
unsigned int g_free_cnt = 0;

void* safe_malloc(size_t size) {
    ++g_alloc_cnt;
    void* ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "malloc failed!\n");
        exit(-1);
    }
    return ptr;
}

void* safe_realloc(void* oldptr, size_t newsize, size_t oldsize) {
    ++g_alloc_cnt;
    ++g_free_cnt;
    void* ptr = realloc(oldptr, newsize);
    if (!ptr) {
        fprintf(stderr, "realloc failed!\n");
        exit(-1);
    }
    if (newsize > oldsize) {
        memset((char*)ptr + oldsize, 0, newsize - oldsize);
    }
    return ptr;
}

void* safe_calloc(size_t nmemb, size_t size) {
    ++g_alloc_cnt;
    void* ptr =  calloc(nmemb, size);
    if (!ptr) {
        fprintf(stderr, "calloc failed!\n");
        exit(-1);
    }
    return ptr;
}

void* safe_zalloc(size_t size) {
    ++g_alloc_cnt;
    void* ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "malloc failed!\n");
        exit(-1);
    }
    memset(ptr, 0, size);
    return ptr;
}

char* strupper(char* str) {
    char* p = str;
    while (*p != '\0') {
        if (*p >= 'a' && *p <= 'z') {
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
    while(*e) {++e;}
    --e;
    char tmp;
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

bool getboolean(const char* str) {
    if (str == NULL) return false;
    int len = strlen(str);
    if (len == 0) return false;
    switch (len) {
    case 1: return *str == '1' || *str == 'y' || *str == 'Y';
    case 2: return stricmp(str, "on") == 0;
    case 3: return stricmp(str, "yes") == 0;
    case 4: return stricmp(str, "true") == 0;
    case 6: return stricmp(str, "enable") == 0;
    default: return false;
    }
}
