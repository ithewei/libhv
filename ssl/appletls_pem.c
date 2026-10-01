#include "appletls_pem.h"
#include "hssl.h"

#if defined(WITH_APPLETLS) || defined(APPLETLS_PEM_TESTING)

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const unsigned char* ptr;
    const unsigned char* end;
} der_cursor_t;

static const unsigned char rsa_encryption_oid[] = {
    0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01
};

static int read_file(const char* path, unsigned char** data, size_t* len) {
    FILE* fp = NULL;
    long file_len;
    unsigned char* buffer = NULL;

    if (path == NULL || data == NULL || len == NULL) return APPLETLS_PEM_ERROR_FORMAT;
    *data = NULL;
    *len = 0;

    fp = fopen(path, "rb");
    if (fp == NULL) return APPLETLS_PEM_ERROR_IO;
    if (fseek(fp, 0, SEEK_END) != 0) goto io_error;
    file_len = ftell(fp);
    if (file_len < 0) goto io_error;
    if ((unsigned long)file_len > APPLETLS_PEM_MAX_FILE_SIZE) {
        fclose(fp);
        return APPLETLS_PEM_ERROR_LIMIT;
    }
    if (file_len == 0 || fseek(fp, 0, SEEK_SET) != 0) goto io_error;

    buffer = (unsigned char*)malloc((size_t)file_len + 1u);
    if (buffer == NULL) {
        fclose(fp);
        return APPLETLS_PEM_ERROR_NOMEM;
    }
    if (fread(buffer, 1, (size_t)file_len, fp) != (size_t)file_len) {
        free(buffer);
        goto io_error;
    }
    buffer[file_len] = '\0';
    fclose(fp);
    *data = buffer;
    *len = (size_t)file_len;
    return APPLETLS_PEM_OK;

io_error:
    fclose(fp);
    return APPLETLS_PEM_ERROR_IO;
}

static int is_space_only(const unsigned char* begin, const unsigned char* end) {
    while (begin < end) {
        if (!isspace(*begin)) return 0;
        ++begin;
    }
    return 1;
}

static int base64_value(unsigned char ch) {
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9') return ch - '0' + 52;
    if (ch == '+') return 62;
    if (ch == '/') return 63;
    return -1;
}

static int decode_base64(const unsigned char* begin, const unsigned char* end, appletls_der_t* der) {
    unsigned char quartet[4];
    size_t quartet_len = 0;
    size_t useful = 0;
    size_t out_len = 0;
    int padded = 0;
    unsigned char* out;
    const unsigned char* ptr;

    for (ptr = begin; ptr < end; ++ptr) {
        if (isspace(*ptr)) continue;
        if (*ptr != '=' && base64_value(*ptr) < 0) return APPLETLS_PEM_ERROR_FORMAT;
        ++useful;
    }
    if (useful == 0 || useful % 4 != 0) return APPLETLS_PEM_ERROR_FORMAT;
    if (useful / 4 > (APPLETLS_PEM_MAX_BLOCK_SIZE + 2u) / 3u) return APPLETLS_PEM_ERROR_LIMIT;

    out = (unsigned char*)malloc((useful / 4) * 3);
    if (out == NULL) return APPLETLS_PEM_ERROR_NOMEM;

    for (ptr = begin; ptr < end; ++ptr) {
        int a, b, c, d;
        if (isspace(*ptr)) continue;
        if (padded) {
            free(out);
            return APPLETLS_PEM_ERROR_FORMAT;
        }
        quartet[quartet_len++] = *ptr;
        if (quartet_len != 4) continue;

        a = base64_value(quartet[0]);
        b = base64_value(quartet[1]);
        c = quartet[2] == '=' ? -2 : base64_value(quartet[2]);
        d = quartet[3] == '=' ? -2 : base64_value(quartet[3]);
        if (a < 0 || b < 0 || c == -1 || d == -1 || (c == -2 && d != -2)) {
            free(out);
            return APPLETLS_PEM_ERROR_FORMAT;
        }
        out[out_len++] = (unsigned char)((a << 2) | (b >> 4));
        if (c == -2) {
            if ((b & 0x0f) != 0) goto invalid_padding;
            padded = 1;
        } else {
            out[out_len++] = (unsigned char)((b << 4) | (c >> 2));
            if (d == -2) {
                if ((c & 0x03) != 0) goto invalid_padding;
                padded = 1;
            } else {
                out[out_len++] = (unsigned char)((c << 6) | d);
            }
        }
        quartet_len = 0;
    }
    if (quartet_len != 0 || out_len == 0 || out_len > APPLETLS_PEM_MAX_BLOCK_SIZE) {
        free(out);
        return out_len > APPLETLS_PEM_MAX_BLOCK_SIZE ? APPLETLS_PEM_ERROR_LIMIT : APPLETLS_PEM_ERROR_FORMAT;
    }
    der->data = out;
    der->len = out_len;
    return APPLETLS_PEM_OK;

invalid_padding:
    free(out);
    return APPLETLS_PEM_ERROR_FORMAT;
}

static int der_read_length(der_cursor_t* cursor, size_t* len) {
    unsigned char first;
    size_t value = 0;
    size_t bytes;
    size_t i;

    if (cursor->ptr >= cursor->end) return APPLETLS_PEM_ERROR_FORMAT;
    first = *cursor->ptr++;
    if ((first & 0x80) == 0) {
        *len = first;
        return (size_t)(cursor->end - cursor->ptr) >= *len ? APPLETLS_PEM_OK : APPLETLS_PEM_ERROR_FORMAT;
    }
    bytes = first & 0x7f;
    if (bytes == 0 || bytes > sizeof(size_t) || (size_t)(cursor->end - cursor->ptr) < bytes) {
        return APPLETLS_PEM_ERROR_FORMAT;
    }
    if (*cursor->ptr == 0) return APPLETLS_PEM_ERROR_FORMAT;
    for (i = 0; i < bytes; ++i) {
        if (value > (SIZE_MAX >> 8)) return APPLETLS_PEM_ERROR_LIMIT;
        value = (value << 8) | *cursor->ptr++;
    }
    if (value < 128 || (size_t)(cursor->end - cursor->ptr) < value) return APPLETLS_PEM_ERROR_FORMAT;
    *len = value;
    return APPLETLS_PEM_OK;
}

static int der_read_tlv(der_cursor_t* cursor, unsigned char expected_tag, der_cursor_t* value) {
    size_t len;
    int ret;
    if (cursor->ptr >= cursor->end || *cursor->ptr++ != expected_tag) return APPLETLS_PEM_ERROR_FORMAT;
    ret = der_read_length(cursor, &len);
    if (ret != APPLETLS_PEM_OK) return ret;
    value->ptr = cursor->ptr;
    value->end = cursor->ptr + len;
    cursor->ptr += len;
    return APPLETLS_PEM_OK;
}

static int der_integer_is_minimal(const der_cursor_t* integer) {
    size_t len = (size_t)(integer->end - integer->ptr);
    if (len == 0 || (integer->ptr[0] & 0x80) != 0) return 0;
    if (len > 1 && integer->ptr[0] == 0 && (integer->ptr[1] & 0x80) == 0) return 0;
    return 1;
}

static int validate_pkcs1_rsa(const unsigned char* data, size_t len) {
    der_cursor_t outer = {data, data + len};
    der_cursor_t sequence;
    der_cursor_t integer;
    size_t i;
    int ret = der_read_tlv(&outer, 0x30, &sequence);
    if (ret != APPLETLS_PEM_OK || outer.ptr != outer.end) return APPLETLS_PEM_ERROR_FORMAT;
    ret = der_read_tlv(&sequence, 0x02, &integer);
    if (ret != APPLETLS_PEM_OK || !der_integer_is_minimal(&integer) ||
        integer.end - integer.ptr != 1 || integer.ptr[0] != 0) {
        return APPLETLS_PEM_ERROR_FORMAT;
    }
    for (i = 0; i < 8; ++i) {
        ret = der_read_tlv(&sequence, 0x02, &integer);
        if (ret != APPLETLS_PEM_OK || !der_integer_is_minimal(&integer)) return APPLETLS_PEM_ERROR_FORMAT;
    }
    return sequence.ptr == sequence.end ? APPLETLS_PEM_OK : APPLETLS_PEM_ERROR_FORMAT;
}

static int copy_der(const unsigned char* data, size_t len, appletls_der_t* out) {
    unsigned char* copy;
    if (len == 0 || len > APPLETLS_PEM_MAX_BLOCK_SIZE) return APPLETLS_PEM_ERROR_LIMIT;
    copy = (unsigned char*)malloc(len);
    if (copy == NULL) return APPLETLS_PEM_ERROR_NOMEM;
    memcpy(copy, data, len);
    out->data = copy;
    out->len = len;
    return APPLETLS_PEM_OK;
}

static int unwrap_pkcs8_rsa(const unsigned char* data, size_t len, appletls_der_t* pkcs1_key) {
    der_cursor_t outer = {data, data + len};
    der_cursor_t sequence;
    der_cursor_t version;
    der_cursor_t algorithm;
    der_cursor_t oid;
    der_cursor_t null_value;
    der_cursor_t private_key;
    int ret = der_read_tlv(&outer, 0x30, &sequence);
    if (ret != APPLETLS_PEM_OK || outer.ptr != outer.end) return APPLETLS_PEM_ERROR_FORMAT;
    ret = der_read_tlv(&sequence, 0x02, &version);
    if (ret != APPLETLS_PEM_OK || version.end - version.ptr != 1 || version.ptr[0] != 0) return APPLETLS_PEM_ERROR_FORMAT;
    ret = der_read_tlv(&sequence, 0x30, &algorithm);
    if (ret != APPLETLS_PEM_OK) return ret;
    ret = der_read_tlv(&algorithm, 0x06, &oid);
    if (ret != APPLETLS_PEM_OK) return ret;
    if ((size_t)(oid.end - oid.ptr) != sizeof(rsa_encryption_oid) ||
        memcmp(oid.ptr, rsa_encryption_oid, sizeof(rsa_encryption_oid)) != 0) {
        return APPLETLS_PEM_ERROR_UNSUPPORTED;
    }
    if (algorithm.ptr != algorithm.end) {
        ret = der_read_tlv(&algorithm, 0x05, &null_value);
        if (ret != APPLETLS_PEM_OK || null_value.ptr != null_value.end || algorithm.ptr != algorithm.end) {
            return APPLETLS_PEM_ERROR_FORMAT;
        }
    }
    ret = der_read_tlv(&sequence, 0x04, &private_key);
    if (ret != APPLETLS_PEM_OK || sequence.ptr != sequence.end) return APPLETLS_PEM_ERROR_FORMAT;
    ret = validate_pkcs1_rsa(private_key.ptr, (size_t)(private_key.end - private_key.ptr));
    if (ret != APPLETLS_PEM_OK) return ret;
    return copy_der(private_key.ptr, (size_t)(private_key.end - private_key.ptr), pkcs1_key);
}

static const unsigned char* find_bytes(const unsigned char* begin, const unsigned char* end, const char* needle) {
    size_t needle_len = strlen(needle);
    const unsigned char* ptr;
    if ((size_t)(end - begin) < needle_len) return NULL;
    for (ptr = begin; ptr + needle_len <= end; ++ptr) {
        if (memcmp(ptr, needle, needle_len) == 0) return ptr;
    }
    return NULL;
}

static int append_der(appletls_der_list_t* list, size_t max_count, appletls_der_t* der) {
    appletls_der_t* items;
    size_t new_count;
    if (list->count >= max_count || list->count >= APPLETLS_PEM_MAX_CERTIFICATES) return APPLETLS_PEM_ERROR_LIMIT;
    new_count = list->count + 1;
    if (new_count > SIZE_MAX / sizeof(*items)) return APPLETLS_PEM_ERROR_LIMIT;
    items = (appletls_der_t*)realloc(list->items, new_count * sizeof(*items));
    if (items == NULL) return APPLETLS_PEM_ERROR_NOMEM;
    list->items = items;
    list->items[list->count] = *der;
    list->count = new_count;
    der->data = NULL;
    der->len = 0;
    return APPLETLS_PEM_OK;
}

static int validate_der_object(const unsigned char* data, size_t len) {
    der_cursor_t outer = {data, data + len};
    der_cursor_t sequence;
    int ret = der_read_tlv(&outer, 0x30, &sequence);
    if (ret != APPLETLS_PEM_OK || outer.ptr != outer.end || sequence.ptr == sequence.end) return APPLETLS_PEM_ERROR_FORMAT;
    return APPLETLS_PEM_OK;
}

int appletls_pem_load_certificates(const char* path, int allow_der, size_t max_certificates, appletls_der_list_t* certificates) {
    static const char begin_marker[] = "-----BEGIN CERTIFICATE-----";
    static const char end_marker[] = "-----END CERTIFICATE-----";
    unsigned char* file = NULL;
    size_t file_len = 0;
    const unsigned char* ptr;
    const unsigned char* end;
    int ret;

    if (certificates == NULL || max_certificates == 0) return APPLETLS_PEM_ERROR_FORMAT;
    certificates->items = NULL;
    certificates->count = 0;
    ret = read_file(path, &file, &file_len);
    if (ret != APPLETLS_PEM_OK) return ret;
    ptr = file;
    end = file + file_len;

    if (find_bytes(ptr, end, begin_marker) == NULL) {
        appletls_der_t der = {0};
        if (!allow_der) {
            ret = APPLETLS_PEM_ERROR_FORMAT;
            goto done;
        }
        ret = validate_der_object(file, file_len);
        if (ret == APPLETLS_PEM_OK) ret = copy_der(file, file_len, &der);
        if (ret == APPLETLS_PEM_OK) ret = append_der(certificates, max_certificates, &der);
        appletls_der_free(&der);
        goto done;
    }

    while (ptr < end) {
        const unsigned char* block_begin;
        const unsigned char* block_end;
        appletls_der_t der = {0};
        block_begin = find_bytes(ptr, end, begin_marker);
        if (block_begin == NULL) {
            ret = is_space_only(ptr, end) ? APPLETLS_PEM_OK : APPLETLS_PEM_ERROR_FORMAT;
            break;
        }
        if (!is_space_only(ptr, block_begin)) {
            ret = APPLETLS_PEM_ERROR_FORMAT;
            break;
        }
        block_begin += strlen(begin_marker);
        block_end = find_bytes(block_begin, end, end_marker);
        if (block_end == NULL) {
            ret = APPLETLS_PEM_ERROR_FORMAT;
            break;
        }
        ret = decode_base64(block_begin, block_end, &der);
        if (ret == APPLETLS_PEM_OK) ret = validate_der_object(der.data, der.len);
        if (ret == APPLETLS_PEM_OK) ret = append_der(certificates, max_certificates, &der);
        appletls_der_free(&der);
        if (ret != APPLETLS_PEM_OK) break;
        ptr = block_end + strlen(end_marker);
    }
    if (ret == APPLETLS_PEM_OK && certificates->count == 0) ret = APPLETLS_PEM_ERROR_FORMAT;

done:
    free(file);
    if (ret != APPLETLS_PEM_OK) appletls_der_list_free(certificates);
    return ret;
}

int appletls_pem_load_rsa_private_key(const char* path, appletls_der_t* pkcs1_key) {
    static const char rsa_begin[] = "-----BEGIN RSA PRIVATE KEY-----";
    static const char rsa_end[] = "-----END RSA PRIVATE KEY-----";
    static const char pkcs8_begin[] = "-----BEGIN PRIVATE KEY-----";
    static const char pkcs8_end[] = "-----END PRIVATE KEY-----";
    static const char encrypted_begin[] = "-----BEGIN ENCRYPTED PRIVATE KEY-----";
    static const char ec_begin[] = "-----BEGIN EC PRIVATE KEY-----";
    unsigned char* file = NULL;
    size_t file_len = 0;
    const unsigned char* begin;
    const unsigned char* end;
    const unsigned char* body_end;
    const char* end_marker;
    int pkcs8 = 0;
    int ret;
    appletls_der_t decoded = {0};

    if (pkcs1_key == NULL) return APPLETLS_PEM_ERROR_FORMAT;
    pkcs1_key->data = NULL;
    pkcs1_key->len = 0;
    ret = read_file(path, &file, &file_len);
    if (ret != APPLETLS_PEM_OK) return ret;
    end = file + file_len;
    if (find_bytes(file, end, encrypted_begin) || find_bytes(file, end, ec_begin) ||
        find_bytes(file, end, "Proc-Type: 4,ENCRYPTED")) {
        ret = APPLETLS_PEM_ERROR_UNSUPPORTED;
        goto done;
    }
    begin = find_bytes(file, end, rsa_begin);
    end_marker = rsa_end;
    if (begin == NULL) {
        begin = find_bytes(file, end, pkcs8_begin);
        end_marker = pkcs8_end;
        pkcs8 = 1;
    }
    if (begin == NULL || !is_space_only(file, begin)) {
        ret = APPLETLS_PEM_ERROR_FORMAT;
        goto done;
    }
    begin += strlen(pkcs8 ? pkcs8_begin : rsa_begin);
    body_end = find_bytes(begin, end, end_marker);
    if (body_end == NULL) {
        ret = APPLETLS_PEM_ERROR_FORMAT;
        goto done;
    }
    if (!is_space_only(body_end + strlen(end_marker), end)) {
        ret = APPLETLS_PEM_ERROR_FORMAT;
        goto done;
    }
    ret = decode_base64(begin, body_end, &decoded);
    if (ret != APPLETLS_PEM_OK) goto done;
    if (pkcs8) {
        ret = unwrap_pkcs8_rsa(decoded.data, decoded.len, pkcs1_key);
    } else {
        ret = validate_pkcs1_rsa(decoded.data, decoded.len);
        if (ret == APPLETLS_PEM_OK) {
            *pkcs1_key = decoded;
            decoded.data = NULL;
            decoded.len = 0;
        }
    }

done:
    appletls_der_free(&decoded);
    free(file);
    return ret;
}

void appletls_der_free(appletls_der_t* der) {
    if (der == NULL) return;
    free(der->data);
    der->data = NULL;
    der->len = 0;
}

void appletls_der_list_free(appletls_der_list_t* list) {
    size_t i;
    if (list == NULL) return;
    for (i = 0; i < list->count; ++i) appletls_der_free(&list->items[i]);
    free(list->items);
    list->items = NULL;
    list->count = 0;
}

const char* appletls_pem_error_string(int error) {
    switch (error) {
    case APPLETLS_PEM_OK: return "success";
    case APPLETLS_PEM_ERROR_IO: return "file I/O failed";
    case APPLETLS_PEM_ERROR_FORMAT: return "invalid PEM or DER format";
    case APPLETLS_PEM_ERROR_UNSUPPORTED: return "unsupported key format";
    case APPLETLS_PEM_ERROR_LIMIT: return "input limit exceeded";
    case APPLETLS_PEM_ERROR_NOMEM: return "out of memory";
    default: return "unknown parser error";
    }
}

#endif
