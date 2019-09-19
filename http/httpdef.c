#include "httpdef.h"

#include <string.h>
// strncmp(s1, s2, strlen(s2))
static int mystrcmp(const char* s1, const char* s2) {
    while (*s1 != '\0' && *s2 != '\0' && *s1 == *s2) {++s1;++s2;}
    return *s2 == 0 ? 0 : (*s1-*s2);
}

const char* http_status_str(enum http_status status) {
    switch (status) {
#define XX(num, name, string) case HTTP_STATUS_##name: return #string;
    HTTP_STATUS_MAP(XX)
#undef XX
    default: return "<unknown>";
    }
}

const char* http_method_str(enum http_method method) {
    switch (method) {
#define XX(num, name, string) case HTTP_##name: return #string;
    HTTP_METHOD_MAP(XX)
#undef XX
    default: return "<unknown>";
    }
}

const char* http_content_type_str(enum http_content_type type) {
    switch (type) {
#define XX(name, string, suffix) case name: return #string;
    HTTP_CONTENT_TYPE_MAP(XX)
#undef XX
    default: return "<unknown>";
    }
}

enum http_status http_status_enum(const char* str) {
#define XX(num, name, string) \
    if (strcmp(str, #string) == 0) { \
        return HTTP_STATUS_##name; \
    }
    HTTP_STATUS_MAP(XX)
#undef XX
    return HTTP_CUSTOM_STATUS;
}

enum http_method http_method_enum(const char* str) {
#define XX(num, name, string) \
    if (strcmp(str, #string) == 0) { \
        return HTTP_##name; \
    }
    HTTP_METHOD_MAP(XX)
#undef XX
    return HTTP_CUSTOM_METHOD;
}

enum http_content_type http_content_type_enum(const char* str) {
    if (!str || !*str) {
        return CONTENT_TYPE_NONE;
    }
#define XX(name, string, suffix) \
    if (mystrcmp(str, #string) == 0) { \
        return name; \
    }
    HTTP_CONTENT_TYPE_MAP(XX)
#undef XX
    return CONTENT_TYPE_UNDEFINED;
}

const char* http_content_type_suffix(enum http_content_type type) {
    switch (type) {
#define XX(name, string, suffix) case name: return #suffix;
    HTTP_CONTENT_TYPE_MAP(XX)
#undef XX
    default: return "<unknown>";
    }
}

const char* http_content_type_str_by_suffix(const char* str) {
    if (!str || !*str) {
        return "";
    }
#define XX(name, string, suffix) \
    if (strcmp(str, #suffix) == 0) { \
        return #string; \
    }
    HTTP_CONTENT_TYPE_MAP(XX)
#undef XX
    return "";
}

enum http_content_type http_content_type_enum_by_suffix(const char* str) {
    if (!str || !*str) {
        return CONTENT_TYPE_NONE;
    }
#define XX(name, string, suffix) \
    if (strcmp(str, #suffix) == 0) { \
        return name; \
    }
    HTTP_CONTENT_TYPE_MAP(XX)
#undef XX
    return CONTENT_TYPE_UNDEFINED;
}
