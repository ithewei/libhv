#ifndef HV_TLV_MESSAGE_HPP_
#define HV_TLV_MESSAGE_HPP_

#include <stdint.h>
#include <string.h>
#include <string>

#include "hloop.h"  // unpack_setting_t, ENCODE_BY_BIG_ENDIAN

namespace hv {

// TLV frame: Type (T bytes) + Length (L bytes) + Value (Length bytes).
// T and L widths are configurable so any binary protocol can reuse this codec.
#define TLV_TYPE_MAX_BYTES  8

typedef struct tlv_setting_s {
    unsigned char type_bytes;    // 0/1/2/4/8, default 4; 0 means no Type field (LV only)
    unsigned char length_bytes;  // 1/2/4, default 4. NOTE: the underlying length-field
                                 // unpacker (event/unpack.c) accumulates the length in a
                                 // 32-bit int, so widths >4 are not usable for framing and
                                 // are clamped to 4 by tlv_unpack_setting().
    bool          big_endian;    // default true (network byte order)

    tlv_setting_s() {
        type_bytes = 4;
        length_bytes = 4;
        big_endian = true;
    }
} tlv_setting_t;

// Max Value length representable by the framing length field (unpacker is 32-bit).
#define TLV_LENGTH_FIELD_MAX_BYTES  4

// Clamp T/L widths to what the codec and framing unpacker support:
//  - type_bytes to TLV_TYPE_MAX_BYTES (the Type buffer is a fixed byte array);
//    without this, pack/unpack would memcpy past type_[8] -> out-of-bounds.
//  - length_bytes to TLV_LENGTH_FIELD_MAX_BYTES (the unpacker length field is 32-bit).
static inline void tlv_setting_normalize(tlv_setting_t* setting) {
    if (setting->type_bytes > TLV_TYPE_MAX_BYTES) {
        setting->type_bytes = TLV_TYPE_MAX_BYTES;
    }
    if (setting->length_bytes > TLV_LENGTH_FIELD_MAX_BYTES) {
        setting->length_bytes = TLV_LENGTH_FIELD_MAX_BYTES;
    }
}

// Fill an unpack_setting_t (UNPACK_BY_LENGTH_FIELD) derived from a tlv_setting_t.
// length_bytes is clamped to TLV_LENGTH_FIELD_MAX_BYTES because the underlying
// unpacker cannot frame wider length fields (see event/unpack.c body_len is uint32).
static inline void tlv_unpack_setting(unpack_setting_t* unpack, const tlv_setting_t* setting,
                                      unsigned int package_max_length = DEFAULT_PACKAGE_MAX_LENGTH) {
    unsigned char length_bytes = setting->length_bytes;
    if (length_bytes > TLV_LENGTH_FIELD_MAX_BYTES) length_bytes = TLV_LENGTH_FIELD_MAX_BYTES;
    memset(unpack, 0, sizeof(unpack_setting_t));
    unpack->mode = UNPACK_BY_LENGTH_FIELD;
    unpack->package_max_length = package_max_length;
    unpack->body_offset = setting->type_bytes + length_bytes;
    unpack->length_field_offset = setting->type_bytes;
    unpack->length_field_bytes = length_bytes;
    unpack->length_field_coding = setting->big_endian ? ENCODE_BY_BIG_ENDIAN : ENCODE_BY_LITTLE_ENDIAN;
}

// TLV codec: Type carried as a byte array (also usable as an integer or sliced
// into sub-fields), Length maintained by setValue, Value a view or owned copy.
class TLVMessage {
public:
    TLVMessage() {
        memset(type_, 0, sizeof(type_));
    }

    // ---- Type: raw bytes / per-byte / integer views ----
    const unsigned char* type() const { return type_; }

    void setType(const void* data, int len) {
        if (len > TLV_TYPE_MAX_BYTES) len = TLV_TYPE_MAX_BYTES;
        memset(type_, 0, sizeof(type_));
        if (data && len > 0) memcpy(type_, data, len);
    }

    unsigned char typeAt(int i) const {
        return (i >= 0 && i < TLV_TYPE_MAX_BYTES) ? type_[i] : 0;
    }

    void setTypeAt(int i, unsigned char b) {
        if (i >= 0 && i < TLV_TYPE_MAX_BYTES) type_[i] = b;
    }

    // Interpret the first type_bytes as an integer, per byte order.
    uint64_t typeInt(const tlv_setting_t* setting) const {
        uint64_t v = 0;
        int n = setting->type_bytes;
        if (setting->big_endian) {
            for (int i = 0; i < n; ++i) v = (v << 8) | type_[i];
        } else {
            for (int i = n - 1; i >= 0; --i) v = (v << 8) | type_[i];
        }
        return v;
    }

    void setTypeInt(uint64_t v, const tlv_setting_t* setting) {
        memset(type_, 0, sizeof(type_));
        int n = setting->type_bytes;
        if (setting->big_endian) {
            for (int i = n - 1; i >= 0; --i) { type_[i] = v & 0xFF; v >>= 8; }
        } else {
            for (int i = 0; i < n; ++i) { type_[i] = v & 0xFF; v >>= 8; }
        }
    }

    // ---- Length (read-only, maintained by setValue) ----
    uint64_t length() const { return value_.size(); }

    // ---- Value ----
    const char* value() const { return value_.data(); }
    const std::string& valueStr() const { return value_; }

    void setValue(const void* data, uint64_t len) {
        value_.assign((const char*)data, (size_t)len);
    }
    void setValue(const std::string& data) { value_ = data; }

    // ---- Codec ----
    // A setting is usable by the codec only if T/L widths fit the buffers.
    static bool valid(const tlv_setting_t* setting) {
        return setting->type_bytes <= TLV_TYPE_MAX_BYTES
            && setting->length_bytes <= TLV_LENGTH_FIELD_MAX_BYTES;
    }

    // Max Value length representable by a length field of length_bytes.
    static uint64_t maxValueLen(const tlv_setting_t* setting) {
        return (setting->length_bytes >= 8) ? UINT64_MAX
                                            : ((uint64_t)1 << (setting->length_bytes * 8)) - 1;
    }

    // Full frame size for the current value under setting; <0 if it can't be
    // represented (bad setting, value too big for length_bytes, or > INT_MAX).
    int packSize(const tlv_setting_t* setting) const {
        if (!valid(setting)) return -1;
        if (value_.size() > maxValueLen(setting)) return -1;
        uint64_t total = (uint64_t)setting->type_bytes + setting->length_bytes + value_.size();
        if (total > 0x7FFFFFFF) return -1;
        return (int)total;
    }

    // Write [T|L|V] into buf; returns bytes written, or <0 if the value can't be
    // represented in length_bytes or the buffer capacity is short.
    int pack(void* buf, int cap, const tlv_setting_t* setting) const {
        int need = packSize(setting);
        if (need < 0) return -1;
        if (!buf || cap < need) return -2;
        unsigned char* p = (unsigned char*)buf;
        // T
        memcpy(p, type_, setting->type_bytes);
        p += setting->type_bytes;
        // L
        writeInt(p, value_.size(), setting->length_bytes, setting->big_endian);
        p += setting->length_bytes;
        // V
        if (!value_.empty()) memcpy(p, value_.data(), value_.size());
        return need;
    }

    // Parse [T|L|V] from buf; Value is copied out. Returns full frame length, or <0.
    int unpack(const void* buf, int len, const tlv_setting_t* setting) {
        if (!valid(setting)) return -1;   // reject out-of-range T/L widths (no overrun)
        int head = setting->type_bytes + setting->length_bytes;
        if (!buf || len < head) return -1;
        const unsigned char* p = (const unsigned char*)buf;
        // T
        memset(type_, 0, sizeof(type_));
        memcpy(type_, p, setting->type_bytes);
        p += setting->type_bytes;
        // L
        uint64_t vlen = readInt(p, setting->length_bytes, setting->big_endian);
        p += setting->length_bytes;
        // V
        if ((uint64_t)len < (uint64_t)head + vlen) return -2;
        value_.assign((const char*)p, (size_t)vlen);
        return head + (int)vlen;
    }

private:
    static void writeInt(unsigned char* p, uint64_t v, int bytes, bool big_endian) {
        if (big_endian) {
            for (int i = bytes - 1; i >= 0; --i) { p[i] = v & 0xFF; v >>= 8; }
        } else {
            for (int i = 0; i < bytes; ++i) { p[i] = v & 0xFF; v >>= 8; }
        }
    }
    static uint64_t readInt(const unsigned char* p, int bytes, bool big_endian) {
        uint64_t v = 0;
        if (big_endian) {
            for (int i = 0; i < bytes; ++i) v = (v << 8) | p[i];
        } else {
            for (int i = bytes - 1; i >= 0; --i) v = (v << 8) | p[i];
        }
        return v;
    }

private:
    unsigned char type_[TLV_TYPE_MAX_BYTES];
    std::string   value_;
};

} // namespace hv

#endif // HV_TLV_MESSAGE_HPP_
