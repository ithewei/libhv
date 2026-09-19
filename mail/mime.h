#ifndef HV_MAIL_MIME_H_
#define HV_MAIL_MIME_H_

#include <stddef.h>
#include "hexport.h"

/*
 * MIME message assembly (for sending) and parsing (for receiving),
 * shared by the SMTP and IMAP clients.
 *
 * NOTE: charset is not transcoded here; parsed text is returned as-is with
 * the declared charset noted, to avoid an iconv dependency.
 */

// email address: "name" <addr>
typedef struct mail_addr_s {
    char* name;   // display name, may be NULL
    char* addr;   // mailbox address user@host
} mail_addr_t;

// attachment / message part
typedef struct mail_attachment_s {
    char*  filename;      // attachment filename, may be NULL for inline parts
    char*  content_type;  // e.g. "application/octet-stream"; NULL => guessed by filename
    void*  data;
    size_t size;
} mail_attachment_t;

// a whole mail (used for both sending and received-parse result)
typedef struct mail_s {
    mail_addr_t  from;
    mail_addr_t* to;    int to_count;
    mail_addr_t* cc;    int cc_count;
    char* subject;
    char* text_body;    // text/plain, may be NULL
    char* html_body;    // text/html,  may be NULL
    mail_attachment_t* attachments;  int attachment_count;
    char* date;         // Date header (receive side), may be NULL
} mail_t;

BEGIN_EXTERN_C

// ---- helpers for building a mail_t (sending side) ----
// All setters copy the input; call mail_clear to free.
HV_EXPORT void mail_set_from(mail_t* mail, const char* addr, const char* name);
HV_EXPORT void mail_add_to  (mail_t* mail, const char* addr, const char* name);
HV_EXPORT void mail_add_cc  (mail_t* mail, const char* addr, const char* name);
HV_EXPORT void mail_set_subject(mail_t* mail, const char* subject);
HV_EXPORT void mail_set_text(mail_t* mail, const char* text);
HV_EXPORT void mail_set_html(mail_t* mail, const char* html);
HV_EXPORT void mail_add_attachment(mail_t* mail, const char* filename,
                                   const char* content_type,
                                   const void* data, size_t size);
// Free everything allocated inside mail (does not free mail itself).
HV_EXPORT void mail_clear(mail_t* mail);

// ---- assembly (sending side) ----
// Build the full RFC 5322 / MIME message (headers + body) into a heap string.
// Caller frees the returned buffer with free(). Returns NULL on error.
HV_EXPORT char* mime_build(const mail_t* mail);

// ---- parsing (receiving side) ----
// Parse a raw RFC 5322 / MIME message into mail (which must be zeroed first).
// The parsed fields are heap-allocated; free with mail_clear.
// @retval 0 on success, <0 on error.
HV_EXPORT int mime_parse(const char* data, size_t size, mail_t* mail);

// ---- content-transfer-encoding codecs (exposed for reuse/tests) ----
// quoted-printable decode; returns decoded length, writes into out (>= inlen).
HV_EXPORT int mime_qp_decode(const char* in, int inlen, char* out);

// RFC 2047 encoded-word for a header value ("=?utf-8?B?...?="). Returns the
// number of bytes written (excluding the terminating '\0'); out must be large
// enough (BASE64 of in plus a small fixed overhead).
HV_EXPORT int mime_encode_word(const char* in, int inlen, char* out, int outlen);

END_EXTERN_C

#endif // HV_MAIL_MIME_H_
