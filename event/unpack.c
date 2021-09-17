#include "unpack.h"
#include "hevent.h"
#include "herr.h"
#include "hlog.h"
#include "hmath.h"

int hio_unpack(hio_t* io, void* buf, int readbytes) {
    unpack_setting_t* setting = io->unpack_setting;
    switch(setting->mode) {
    case UNPACK_BY_FIXED_LENGTH:
        return hio_unpack_by_fixed_length(io, buf, readbytes);
    case UNPACK_BY_DELIMITER:
        return hio_unpack_by_delimiter(io, buf, readbytes);
    case UNPACK_BY_LENGTH_FIELD:
        return hio_unpack_by_length_field(io, buf, readbytes);
    default:
        hio_read_cb(io, buf, readbytes);
        return readbytes;
    }
}

int hio_unpack_by_fixed_length(hio_t* io, void* buf, int readbytes) {
    const unsigned char* sp = (const unsigned char*)io->readbuf.base;
    assert(buf == sp + io->readbuf.offset);
    const unsigned char* ep = sp + io->readbuf.offset + readbytes;
    unpack_setting_t* setting = io->unpack_setting;

    int fixed_length = setting->fixed_length;
    assert(io->readbuf.len >= fixed_length);

    const unsigned char* p = sp;
    int remain = ep - p;
    int handled = 0;
    while (remain >= fixed_length) {
        hio_read_cb(io, (void*)p, fixed_length);
        handled += fixed_length;
        p += fixed_length;
        remain -= fixed_length;
    }

    io->readbuf.offset = remain;
    if (remain) {
        // [p, p+remain] => [base, base+remain]
        if (p != (unsigned char*)io->readbuf.base) {
            memmove(io->readbuf.base, p, remain);
        }
    }

    return handled;
}

int hio_unpack_by_delimiter(hio_t* io, void* buf, int readbytes) {
    const unsigned char* sp = (const unsigned char*)io->readbuf.base;
    assert(buf == sp + io->readbuf.offset);
    const unsigned char* ep = sp + io->readbuf.offset + readbytes;
    unpack_setting_t* setting = io->unpack_setting;

    unsigned char* delimiter = setting->delimiter;
    int delimiter_bytes = setting->delimiter_bytes;

    // [offset - package_eof_bytes + 1, offset + readbytes]
    const unsigned char* p = sp + io->readbuf.offset - delimiter_bytes + 1;
    if (p < sp) p = sp;
    int remain = ep - p;
    int handled = 0;
    int i = 0;
    while (remain >= delimiter_bytes) {
        for (i = 0; i < delimiter_bytes; ++i) {
            if (p[i] != delimiter[i]) {
                goto not_match;
            }
        }
match:
        p += delimiter_bytes;
        remain -= delimiter_bytes;
        hio_read_cb(io, (void*)sp, p - sp);
        handled += p - sp;
        sp = p;
        continue;
not_match:
        ++p;
        --remain;
    }

    remain = ep - sp;
    io->readbuf.offset = remain;
    if (remain) {
        // [sp, sp+remain] => [base, base+remain]
        if (sp != (unsigned char*)io->readbuf.base) {
            memmove(io->readbuf.base, sp, remain);
        }
        if (io->readbuf.offset == io->readbuf.len) {
            if (io->readbuf.len >= setting->package_max_length) {
                hloge("recv package over %d bytes!", (int)setting->package_max_length);
                io->error = ERR_OVER_LIMIT;
                hio_close(io);
                return -1;
            }
            io->readbuf.len = MIN(io->readbuf.len * 2, setting->package_max_length);
            io->readbuf.base = (char*)safe_realloc(io->readbuf.base, io->readbuf.len, io->readbuf.offset);
        }
    }

    return handled;
}

int hio_unpack_by_length_field(hio_t* io, void* buf, int readbytes) {
    const unsigned char* sp = (const unsigned char*)io->readbuf.base;
    assert(buf == sp + io->readbuf.offset);
    const unsigned char* ep = sp + io->readbuf.offset + readbytes;
    unpack_setting_t* setting = io->unpack_setting;

    const unsigned char* p = sp;
    int remain = ep - p;
    int handled = 0;
    unsigned int head_len = setting->body_offset;
    unsigned int body_len = 0;
    unsigned int package_len = head_len + body_len;
    const unsigned char* lp = NULL;
    while (remain >= setting->body_offset) {
        body_len = 0;
        lp = p + setting->length_field_offset;
        if (setting->length_field_coding == BIG_ENDIAN) {
            for (int i = 0; i < setting->length_field_bytes; ++i) {
                body_len = (body_len << 8) | (unsigned int)*lp++;
            }
        }
        else if (setting->length_field_coding == LITTLE_ENDIAN) {
            for (int i = 0; i < setting->length_field_bytes; ++i) {
                body_len |= ((unsigned int)*lp++) << (i * 8);
            }
        }
        else if (setting->length_field_coding == ENCODE_BY_VARINT) {
            int varint_bytes = ep - lp;
            body_len = varint_decode(lp, &varint_bytes);
            if (varint_bytes == 0) break;
            if (varint_bytes == -1) {
                hloge("varint is too big!");
                io->error = ERR_OVER_LIMIT;
                hio_close(io);
                return -1;
            }
            head_len = setting->body_offset + varint_bytes - setting->length_field_bytes;
        }
        package_len = head_len + body_len;
        if (remain >= package_len) {
            hio_read_cb(io, (void*)p, package_len);
            handled += package_len;
            p += package_len;
            remain -= package_len;
        } else {
            break;
        }
    }

    io->readbuf.offset = remain;
    if (remain) {
        // [p, p+remain] => [base, base+remain]
        if (p != (unsigned char*)io->readbuf.base) {
            memmove(io->readbuf.base, p, remain);
        }
        if (package_len > io->readbuf.len) {
            if (package_len > setting->package_max_length) {
                hloge("package length over %d bytes!", (int)setting->package_max_length);
                io->error = ERR_OVER_LIMIT;
                hio_close(io);
                return -1;
            }
            io->readbuf.len *= 2;
            io->readbuf.len = LIMIT(package_len, io->readbuf.len, setting->package_max_length);
            io->readbuf.base = (char*)safe_realloc(io->readbuf.base, io->readbuf.len, io->readbuf.offset);
        }
    }

    return handled;
}
