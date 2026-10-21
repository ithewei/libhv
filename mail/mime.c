#include "mime.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "hbase.h"
#include "base64.h"

// ---- small helpers ----

static char* mime_strndup(const char* s, size_t n) {
    char* p = (char*)malloc(n + 1);
    if (p == NULL) return NULL;
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static char* mime_strdup(const char* s) {
    return s ? mime_strndup(s, strlen(s)) : NULL;
}

// growable buffer for message assembly
typedef struct {
    char*  base;
    size_t len;
    size_t cap;
} membuf_t;

static int membuf_ensure(membuf_t* buf, size_t need) {
    if (buf->len + need <= buf->cap) return 0;
    size_t newcap = buf->cap ? buf->cap * 2 : 4096;
    while (newcap < buf->len + need) newcap *= 2;
    char* p = (char*)realloc(buf->base, newcap);
    if (p == NULL) return -1;
    buf->base = p;
    buf->cap = newcap;
    return 0;
}

static int membuf_append(membuf_t* buf, const char* data, size_t len) {
    if (membuf_ensure(buf, len) != 0) return -1;
    memcpy(buf->base + buf->len, data, len);
    buf->len += len;
    return 0;
}

static int membuf_puts(membuf_t* buf, const char* s) {
    return membuf_append(buf, s, strlen(s));
}

// ---- codecs ----

// base64 decode that tolerates MIME line breaks / whitespace: hv_base64_decode
// rejects CR/LF, so strip whitespace into a scratch buffer first.
// Returns decoded length, or -1 on error.
static int mime_base64_decode(const char* in, int inlen, unsigned char* out) {
    char* tmp = (char*)malloc(inlen + 1);
    if (tmp == NULL) return -1;
    int n = 0;
    for (int i = 0; i < inlen; ++i) {
        char c = in[i];
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t') continue;
        tmp[n++] = c;
    }
    int ret = hv_base64_decode(tmp, n, out);
    free(tmp);
    return ret;
}

int mime_qp_decode(const char* in, int inlen, char* out) {
    int o = 0;
    for (int i = 0; i < inlen; ++i) {
        char c = in[i];
        if (c == '=' && i + 2 < inlen) {
            if (in[i + 1] == '\r' && in[i + 2] == '\n') {
                // soft line break
                i += 2;
            } else {
                char hex[3] = { in[i + 1], in[i + 2], 0 };
                out[o++] = (char)strtol(hex, NULL, 16);
                i += 2;
            }
        } else if (c == '=' && i + 1 < inlen && in[i + 1] == '\n') {
            i += 1; // soft line break (LF only)
        } else {
            out[o++] = c;
        }
    }
    return o;
}

int mime_encode_word(const char* in, int inlen, char* out, int outlen) {
    // "=?utf-8?B?<base64>?="
    int need = 12 + BASE64_ENCODE_OUT_SIZE(inlen) + 1;
    if (outlen < need) return -1;
    int n = snprintf(out, outlen, "=?utf-8?B?");
    n += hv_base64_encode((const unsigned char*)in, inlen, out + n);
    n += snprintf(out + n, outlen - n, "?=");
    return n;
}

// Decode an RFC 2047 header value that may contain encoded-words
// "=?charset?B?..?=" (base64) or "=?charset?Q?..?=" (quoted-printable-like).
// Non-encoded text is copied through. Returns a heap string (caller frees),
// charset is not converted (returned bytes are the decoded payload as-is).
static char* mime_decode_word(const char* in, int inlen) {
    char* out = (char*)malloc(inlen + 1);
    if (out == NULL) return NULL;
    int o = 0;
    int i = 0;
    while (i < inlen) {
        // look for "=?"
        if (i + 1 < inlen && in[i] == '=' && in[i + 1] == '?') {
            // parse =?charset?E?text?=
            const char* p = in + i + 2;
            const char* end = in + inlen;
            const char* q1 = memchr(p, '?', end - p);          // after charset
            if (q1 && q1 + 2 < end && q1[2] == '?') {
                char enc = q1[1];
                const char* text = q1 + 3;
                // find closing "?="
                const char* close = text;
                while (close + 1 < end && !(close[0] == '?' && close[1] == '=')) close++;
                if (close + 1 < end) {
                    int tlen = (int)(close - text);
                    if (enc == 'B' || enc == 'b') {
                        o += hv_base64_decode(text, tlen, (unsigned char*)out + o);
                    } else if (enc == 'Q' || enc == 'q') {
                        // Q-encoding: '_' => space, '=XX' => byte
                        for (int k = 0; k < tlen; ++k) {
                            if (text[k] == '_') {
                                out[o++] = ' ';
                            } else if (text[k] == '=' && k + 2 < tlen) {
                                char hex[3] = { text[k + 1], text[k + 2], 0 };
                                out[o++] = (char)strtol(hex, NULL, 16);
                                k += 2;
                            } else {
                                out[o++] = text[k];
                            }
                        }
                    }
                    i = (int)(close + 2 - in);
                    continue;
                }
            }
        }
        out[o++] = in[i++];
    }
    out[o] = '\0';
    return out;
}

// Encode a header value: if it contains non-ASCII, use RFC 2047 encoded-word.
// Any CR/LF (or other control chars) force encoding too, to prevent header
// injection (e.g. a subject/name carrying "\r\nBcc: ...").
static void append_header_value(membuf_t* buf, const char* value) {
    int needs_encoding = 0;
    for (const char* p = value; *p; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c >= 0x80 || c == '\r' || c == '\n' || c == '\t') { needs_encoding = 1; break; }
    }
    if (!needs_encoding) {
        membuf_puts(buf, value);
        return;
    }
    int inlen = (int)strlen(value);
    int outlen = 16 + BASE64_ENCODE_OUT_SIZE(inlen);
    char* enc = (char*)malloc(outlen);
    if (enc == NULL) return;   // drop rather than emit an unsafe raw value
    int n = mime_encode_word(value, inlen, enc, outlen);
    if (n > 0) membuf_append(buf, enc, n);
    free(enc);
}

// append "name <addr>" or "addr"
static void append_addr(membuf_t* buf, const mail_addr_t* addr) {
    if (addr->name && addr->name[0]) {
        append_header_value(buf, addr->name);
        membuf_puts(buf, " <");
        membuf_puts(buf, addr->addr ? addr->addr : "");
        membuf_puts(buf, ">");
    } else {
        membuf_puts(buf, addr->addr ? addr->addr : "");
    }
}

static void append_addr_list(membuf_t* buf, const char* header,
                             const mail_addr_t* addrs, int count) {
    if (count <= 0) return;
    membuf_puts(buf, header);
    membuf_puts(buf, ": ");
    for (int i = 0; i < count; ++i) {
        if (i) membuf_puts(buf, ", ");
        append_addr(buf, &addrs[i]);
    }
    membuf_puts(buf, "\r\n");
}

// ---- mail_t builders ----

void mail_set_from(mail_t* mail, const char* addr, const char* name) {
    free(mail->from.addr);
    free(mail->from.name);
    mail->from.addr = mime_strdup(addr);
    mail->from.name = mime_strdup(name);
}

static void add_addr(mail_addr_t** list, int* count, const char* addr, const char* name) {
    mail_addr_t* p = (mail_addr_t*)realloc(*list, sizeof(mail_addr_t) * (*count + 1));
    if (p == NULL) return;
    *list = p;
    p[*count].addr = mime_strdup(addr);
    p[*count].name = mime_strdup(name);
    (*count)++;
}

void mail_add_to(mail_t* mail, const char* addr, const char* name) {
    add_addr(&mail->to, &mail->to_count, addr, name);
}

void mail_add_cc(mail_t* mail, const char* addr, const char* name) {
    add_addr(&mail->cc, &mail->cc_count, addr, name);
}

void mail_set_subject(mail_t* mail, const char* subject) {
    free(mail->subject);
    mail->subject = mime_strdup(subject);
}

void mail_set_text(mail_t* mail, const char* text) {
    free(mail->text_body);
    mail->text_body = mime_strdup(text);
}

void mail_set_html(mail_t* mail, const char* html) {
    free(mail->html_body);
    mail->html_body = mime_strdup(html);
}

void mail_add_attachment(mail_t* mail, const char* filename,
                         const char* content_type,
                         const void* data, size_t size) {
    mail_attachment_t* p = (mail_attachment_t*)realloc(
        mail->attachments, sizeof(mail_attachment_t) * (mail->attachment_count + 1));
    if (p == NULL) return;
    mail->attachments = p;
    mail_attachment_t* a = &p[mail->attachment_count];
    a->filename = mime_strdup(filename);
    a->content_type = mime_strdup(content_type);
    a->data = malloc(size);
    if (a->data) memcpy(a->data, data, size);
    a->size = a->data ? size : 0;
    mail->attachment_count++;
}

void mail_clear(mail_t* mail) {
    if (mail == NULL) return;
    free(mail->from.addr);
    free(mail->from.name);
    for (int i = 0; i < mail->to_count; ++i) {
        free(mail->to[i].addr);
        free(mail->to[i].name);
    }
    free(mail->to);
    for (int i = 0; i < mail->cc_count; ++i) {
        free(mail->cc[i].addr);
        free(mail->cc[i].name);
    }
    free(mail->cc);
    free(mail->subject);
    free(mail->text_body);
    free(mail->html_body);
    for (int i = 0; i < mail->attachment_count; ++i) {
        free(mail->attachments[i].filename);
        free(mail->attachments[i].content_type);
        free(mail->attachments[i].data);
    }
    free(mail->attachments);
    free(mail->date);
    memset(mail, 0, sizeof(mail_t));
}

// ---- assembly ----

static const char* guess_content_type(const char* filename) {
    if (filename == NULL) return "application/octet-stream";
    const char* dot = strrchr(filename, '.');
    if (dot == NULL) return "application/octet-stream";
    dot++;
    if (stricmp(dot, "txt") == 0)  return "text/plain";
    if (stricmp(dot, "html") == 0) return "text/html";
    if (stricmp(dot, "htm") == 0)  return "text/html";
    if (stricmp(dot, "jpg") == 0 || stricmp(dot, "jpeg") == 0) return "image/jpeg";
    if (stricmp(dot, "png") == 0)  return "image/png";
    if (stricmp(dot, "gif") == 0)  return "image/gif";
    if (stricmp(dot, "pdf") == 0)  return "application/pdf";
    if (stricmp(dot, "zip") == 0)  return "application/zip";
    if (stricmp(dot, "json") == 0) return "application/json";
    return "application/octet-stream";
}

// append base64 of data, wrapped at 76 chars per line
static void append_base64_wrapped(membuf_t* buf, const void* data, size_t size) {
    if (size == 0) return;
    int encoded_size = BASE64_ENCODE_OUT_SIZE(size);
    char* enc = (char*)malloc(encoded_size + 1);
    if (enc == NULL) return;
    int n = hv_base64_encode((const unsigned char*)data, (unsigned int)size, enc);
    for (int i = 0; i < n; i += 76) {
        int line = (n - i) < 76 ? (n - i) : 76;
        membuf_append(buf, enc + i, line);
        membuf_puts(buf, "\r\n");
    }
    free(enc);
}

static void append_common_headers(membuf_t* buf, const mail_t* mail) {
    // From / To / Cc
    membuf_puts(buf, "From: ");
    append_addr(buf, &mail->from);
    membuf_puts(buf, "\r\n");
    append_addr_list(buf, "To", mail->to, mail->to_count);
    append_addr_list(buf, "Cc", mail->cc, mail->cc_count);
    // Subject
    membuf_puts(buf, "Subject: ");
    append_header_value(buf, mail->subject ? mail->subject : "");
    membuf_puts(buf, "\r\n");
    membuf_puts(buf, "MIME-Version: 1.0\r\n");
}

char* mime_build(const mail_t* mail) {
    membuf_t buf;
    memset(&buf, 0, sizeof(buf));

    int has_attach = mail->attachment_count > 0;
    int has_html   = mail->html_body && mail->html_body[0];
    int has_text   = mail->text_body && mail->text_body[0];

    append_common_headers(&buf, mail);

    if (!has_attach && !has_html) {
        // simple text/plain
        membuf_puts(&buf, "Content-Type: text/plain; charset=utf-8\r\n\r\n");
        membuf_puts(&buf, has_text ? mail->text_body : "");
        membuf_puts(&buf, "\r\n");
    } else {
        char boundary[64];
        snprintf(boundary, sizeof(boundary), "----=_libhv_%p_%d", (void*)mail, (int)buf.len);
        char altboundary[72];
        snprintf(altboundary, sizeof(altboundary), "%s_alt", boundary);

        membuf_puts(&buf, "Content-Type: multipart/mixed; boundary=\"");
        membuf_puts(&buf, boundary);
        membuf_puts(&buf, "\"\r\n\r\n");

        // body part (text + optional html as multipart/alternative)
        membuf_puts(&buf, "--"); membuf_puts(&buf, boundary); membuf_puts(&buf, "\r\n");
        if (has_html && has_text) {
            membuf_puts(&buf, "Content-Type: multipart/alternative; boundary=\"");
            membuf_puts(&buf, altboundary);
            membuf_puts(&buf, "\"\r\n\r\n");
            membuf_puts(&buf, "--"); membuf_puts(&buf, altboundary); membuf_puts(&buf, "\r\n");
            membuf_puts(&buf, "Content-Type: text/plain; charset=utf-8\r\n\r\n");
            membuf_puts(&buf, mail->text_body);
            membuf_puts(&buf, "\r\n--"); membuf_puts(&buf, altboundary); membuf_puts(&buf, "\r\n");
            membuf_puts(&buf, "Content-Type: text/html; charset=utf-8\r\n\r\n");
            membuf_puts(&buf, mail->html_body);
            membuf_puts(&buf, "\r\n--"); membuf_puts(&buf, altboundary); membuf_puts(&buf, "--\r\n");
        } else if (has_html) {
            membuf_puts(&buf, "Content-Type: text/html; charset=utf-8\r\n\r\n");
            membuf_puts(&buf, mail->html_body);
            membuf_puts(&buf, "\r\n");
        } else {
            membuf_puts(&buf, "Content-Type: text/plain; charset=utf-8\r\n\r\n");
            membuf_puts(&buf, has_text ? mail->text_body : "");
            membuf_puts(&buf, "\r\n");
        }

        // attachments
        for (int i = 0; i < mail->attachment_count; ++i) {
            const mail_attachment_t* a = &mail->attachments[i];
            const char* ct = (a->content_type && a->content_type[0])
                           ? a->content_type : guess_content_type(a->filename);
            membuf_puts(&buf, "--"); membuf_puts(&buf, boundary); membuf_puts(&buf, "\r\n");
            membuf_puts(&buf, "Content-Type: ");
            membuf_puts(&buf, ct);
            membuf_puts(&buf, "\r\n");
            membuf_puts(&buf, "Content-Transfer-Encoding: base64\r\n");
            membuf_puts(&buf, "Content-Disposition: attachment; filename=\"");
            membuf_puts(&buf, a->filename ? a->filename : "attachment");
            membuf_puts(&buf, "\"\r\n\r\n");
            append_base64_wrapped(&buf, a->data, a->size);
        }

        membuf_puts(&buf, "--"); membuf_puts(&buf, boundary); membuf_puts(&buf, "--\r\n");
    }

    // NUL-terminate for convenience (not counted in a length; caller uses strlen)
    membuf_append(&buf, "", 1);
    return buf.base;
}

// ---- parsing ----

// case-insensitive header field lookup within [start, end); returns value start
// (after "name:") and sets *val_len to value length (trimmed, single line +
// folded continuations collapsed is not done here — value is up to CRLF).
static const char* find_header(const char* start, const char* end,
                               const char* name, int* val_len) {
    size_t namelen = strlen(name);
    const char* p = start;
    while (p < end) {
        const char* line_end = p;
        while (line_end < end && *line_end != '\n') line_end++;
        if ((size_t)(line_end - p) > namelen &&
            strnicmp(p, name, namelen) == 0 && p[namelen] == ':') {
            const char* v = p + namelen + 1;
            while (v < line_end && (*v == ' ' || *v == '\t')) v++;
            const char* ve = line_end;
            if (ve > v && ve[-1] == '\r') ve--;
            *val_len = (int)(ve - v);
            return v;
        }
        // empty line => end of headers
        if (p == line_end || (p + 1 == line_end && *p == '\r')) break;
        p = line_end + 1;
    }
    *val_len = 0;
    return NULL;
}

static char* dup_header(const char* start, const char* end, const char* name) {
    int len = 0;
    const char* v = find_header(start, end, name, &len);
    if (v == NULL || len <= 0) return NULL;
    return mime_decode_word(v, len);
}

// find end of headers (double CRLF); returns pointer to body start, or end.
static const char* find_body(const char* start, const char* end) {
    for (const char* p = start; p + 1 < end; ++p) {
        if (p[0] == '\n' && p[1] == '\n') return p + 2;
        if (p + 3 < end && p[0] == '\r' && p[1] == '\n' && p[2] == '\r' && p[3] == '\n')
            return p + 4;
    }
    return end;
}

// extract boundary="xxx" from a Content-Type value
static int extract_boundary(const char* ct, int ctlen, char* out, int outlen) {
    const char* p = ct;
    const char* end = ct + ctlen;
    while (p < end) {
        if (strnicmp(p, "boundary", 8) == 0) {
            p += 8;
            while (p < end && (*p == ' ' || *p == '=')) p++;
            int quoted = 0;
            if (p < end && *p == '"') { quoted = 1; p++; }
            const char* b = p;
            while (p < end && (quoted ? *p != '"' : (*p != ';' && *p != ' ' && *p != '\r' && *p != '\n'))) p++;
            int len = (int)(p - b);
            if (len <= 0 || len >= outlen) return -1;
            memcpy(out, b, len);
            out[len] = '\0';
            return 0;
        }
        p++;
    }
    return -1;
}

// decode a body part according to Content-Transfer-Encoding into a heap buffer.
// Sets *outlen to the decoded byte length (binary-safe; the buffer is also
// NUL-terminated for text convenience but may contain embedded NULs).
static char* decode_part_body(const char* body, int bodylen,
                              const char* enc, int enclen, int* outlen) {
    if (enc && enclen >= 6 && strnicmp(enc, "base64", 6) == 0) {
        int out_size = BASE64_DECODE_OUT_SIZE(bodylen) + 1;
        char* out = (char*)malloc(out_size);
        if (out == NULL) { *outlen = 0; return NULL; }
        int n = mime_base64_decode(body, bodylen, (unsigned char*)out);
        if (n < 0) n = 0;
        out[n] = '\0';
        *outlen = n;
        return out;
    }
    if (enc && enclen >= 16 && strnicmp(enc, "quoted-printable", 16) == 0) {
        char* out = (char*)malloc(bodylen + 1);
        if (out == NULL) { *outlen = 0; return NULL; }
        int n = mime_qp_decode(body, bodylen, out);
        out[n] = '\0';
        *outlen = n;
        return out;
    }
    // 7bit / 8bit / none
    *outlen = bodylen;
    return mime_strndup(body, bodylen);
}

// parse a single part [start,end); fill text/html/attachment on mail.
static void parse_part(const char* start, const char* end, mail_t* mail);

// parse a multipart body given its boundary.
static void parse_multipart(const char* body, const char* end,
                            const char* boundary, mail_t* mail) {
    char delim[80];
    snprintf(delim, sizeof(delim), "--%s", boundary);
    size_t delimlen = strlen(delim);

    const char* p = body;
    // find first boundary
    while (p < end) {
        if ((size_t)(end - p) >= delimlen && strncmp(p, delim, delimlen) == 0) break;
        while (p < end && *p != '\n') p++;
        if (p < end) p++;
    }
    while (p < end) {
        // p at a boundary line
        p += delimlen;
        if (p + 1 < end && p[0] == '-' && p[1] == '-') break; // closing boundary
        while (p < end && *p != '\n') p++;   // skip to end of boundary line
        if (p < end) p++;
        const char* part_start = p;
        // find next boundary
        const char* q = p;
        const char* next = end;
        while (q < end) {
            if ((size_t)(end - q) >= delimlen && strncmp(q, delim, delimlen) == 0 &&
                (q == body || q[-1] == '\n')) {
                next = q;
                break;
            }
            while (q < end && *q != '\n') q++;
            if (q < end) q++;
        }
        const char* part_end = next;
        // trim trailing CRLF before boundary
        if (part_end > part_start && part_end[-1] == '\n') part_end--;
        if (part_end > part_start && part_end[-1] == '\r') part_end--;
        parse_part(part_start, part_end, mail);
        p = next;
    }
}

static void parse_part(const char* start, const char* end, mail_t* mail) {
    int ctlen = 0;
    const char* ct = find_header(start, end, "Content-Type", &ctlen);
    const char* body = find_body(start, end);

    // nested multipart
    if (ct && ctlen >= 9 && strnicmp(ct, "multipart", 9) == 0) {
        char boundary[64];
        if (extract_boundary(ct, ctlen, boundary, sizeof(boundary)) == 0) {
            parse_multipart(body, end, boundary, mail);
        }
        return;
    }

    int enclen = 0;
    const char* enc = find_header(start, end, "Content-Transfer-Encoding", &enclen);
    int dislen = 0;
    const char* dis = find_header(start, end, "Content-Disposition", &dislen);

    int is_attachment = (dis && dislen >= 10 && strnicmp(dis, "attachment", 10) == 0);

    if (is_attachment) {
        // filename
        char* filename = NULL;
        if (dis) {
            const char* fp = dis;
            const char* de = dis + dislen;
            while (fp < de) {
                if (strnicmp(fp, "filename", 8) == 0) {
                    fp += 8;
                    while (fp < de && (*fp == ' ' || *fp == '=')) fp++;
                    int quoted = 0;
                    if (fp < de && *fp == '"') { quoted = 1; fp++; }
                    const char* fb = fp;
                    while (fp < de && (quoted ? *fp != '"' : (*fp != ';' && *fp != ' '))) fp++;
                    filename = mime_strndup(fb, fp - fb);
                    break;
                }
                fp++;
            }
        }
        int bodylen = (int)(end - body);
        int declen = 0;
        char* decoded = decode_part_body(body, bodylen, enc, enclen, &declen);
        char* ct_dup = ct ? mime_strndup(ct, ctlen) : NULL;
        // strip params after ';' in ct
        if (ct_dup) {
            char* semi = strchr(ct_dup, ';');
            if (semi) *semi = '\0';
        }
        mail_add_attachment(mail, filename ? filename : "attachment",
                            ct_dup, decoded ? decoded : "",
                            decoded ? (size_t)declen : 0);
        free(filename);
        free(ct_dup);
        free(decoded);
        return;
    }

    // text body
    int bodylen = (int)(end - body);
    int declen = 0;
    char* decoded = decode_part_body(body, bodylen, enc, enclen, &declen);
    if (decoded == NULL) return;
    int is_html = (ct && ctlen >= 9 && strnicmp(ct, "text/html", 9) == 0);
    if (is_html) {
        if (mail->html_body == NULL) mail->html_body = decoded;
        else free(decoded);
    } else {
        if (mail->text_body == NULL) mail->text_body = decoded;
        else free(decoded);
    }
}

int mime_parse(const char* data, size_t size, mail_t* mail) {
    if (data == NULL || mail == NULL) return -1;
    const char* start = data;
    const char* end = data + size;

    // top-level headers
    mail->subject = dup_header(start, end, "Subject");
    mail->date = dup_header(start, end, "Date");
    char* from = dup_header(start, end, "From");
    if (from) {
        // naive: take whole From as addr (address extraction kept simple)
        mail->from.addr = from;
    }

    int ctlen = 0;
    const char* ct = find_header(start, end, "Content-Type", &ctlen);
    const char* body = find_body(start, end);

    if (ct && ctlen >= 9 && strnicmp(ct, "multipart", 9) == 0) {
        char boundary[64];
        if (extract_boundary(ct, ctlen, boundary, sizeof(boundary)) == 0) {
            parse_multipart(body, end, boundary, mail);
            return 0;
        }
    }

    // single part
    parse_part(start, end, mail);
    return 0;
}
