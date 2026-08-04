#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "TLVMessage.h"

using namespace hv;

// pack then unpack round-trips type/length/value under a given setting.
static void test_roundtrip(const tlv_setting_t& setting) {
    TLVMessage out;
    out.setTypeInt(0x12345678u & ((setting.type_bytes >= 4) ? 0xFFFFFFFFu : 0xFFFFu), &setting);
    const char* payload = "hello tlv";
    out.setValue(payload, strlen(payload));

    char buf[256];
    int packlen = out.pack(buf, sizeof(buf), &setting);
    assert(packlen == out.packSize(&setting));
    assert(packlen == (int)(setting.type_bytes + setting.length_bytes + strlen(payload)));

    TLVMessage in;
    int framelen = in.unpack(buf, packlen, &setting);
    assert(framelen == packlen);
    assert(in.typeInt(&setting) == out.typeInt(&setting));
    assert(in.length() == strlen(payload));
    assert(in.valueStr() == payload);
    printf("  roundtrip T=%d L=%d %s: OK\n", setting.type_bytes, setting.length_bytes,
           setting.big_endian ? "BE" : "LE");
}

// Type is a byte array that can be sliced into sub-fields (hrpc use case).
static void test_type_slicing() {
    tlv_setting_t setting;
    setting.type_bytes = 8;
    setting.length_bytes = 4;

    TLVMessage tlv;
    tlv.setTypeAt(0, 'h');
    tlv.setTypeAt(1, 'r');
    tlv.setTypeAt(2, 'p');
    tlv.setTypeAt(3, 'c');
    tlv.setTypeAt(4, 1);     // version
    tlv.setTypeAt(5, 2);     // msg type
    tlv.setValue("body", 4);

    char buf[64];
    int packlen = tlv.pack(buf, sizeof(buf), &setting);
    assert(packlen > 0);

    TLVMessage in;
    assert(in.unpack(buf, packlen, &setting) == packlen);
    assert(in.typeAt(0) == 'h' && in.typeAt(3) == 'c');
    assert(in.typeAt(4) == 1 && in.typeAt(5) == 2);
    assert(in.valueStr() == "body");
    printf("  type slicing: OK\n");
}

// Short buffers must fail cleanly, not overrun.
static void test_short_buffer() {
    tlv_setting_t setting; // 4/4/BE
    TLVMessage tlv;
    tlv.setValue("abcd", 4);
    char small[4];
    assert(tlv.pack(small, sizeof(small), &setting) < 0);

    char buf[64];
    int packlen = tlv.pack(buf, sizeof(buf), &setting);
    TLVMessage in;
    assert(in.unpack(buf, packlen - 1, &setting) < 0); // truncated value
    assert(in.unpack(buf, 2, &setting) < 0);           // truncated header
    printf("  short buffer: OK\n");
}

// I2: value larger than length_bytes can represent must be rejected, not
// silently truncated into an inconsistent frame.
static void test_value_too_big() {
    tlv_setting_t s;
    s.type_bytes = 1;
    s.length_bytes = 1;   // max value len = 255
    TLVMessage tlv;
    std::string big(300, 'x');
    tlv.setValue(big);
    assert(tlv.packSize(&s) < 0);           // cannot represent
    char buf[512];
    assert(tlv.pack(buf, sizeof(buf), &s) < 0);
    // a 255-byte value still fits
    tlv.setValue(std::string(255, 'y'));
    assert(tlv.packSize(&s) == 1 + 1 + 255);
    printf("  value too big rejected: OK\n");
}

// I1: T/L widths beyond limits are clamped by tlv_setting_normalize, and the
// codec rejects an out-of-range setting instead of overrunning type_[8].
static void test_width_clamp() {
    // length_bytes > 4 clamps for the framing unpacker (32-bit length field)
    tlv_setting_t s;
    s.type_bytes = 4;
    s.length_bytes = 8;
    unpack_setting_t u;
    tlv_unpack_setting(&u, &s);
    assert(u.length_field_bytes == TLV_LENGTH_FIELD_MAX_BYTES);   // clamped to 4
    assert(u.body_offset == 4 + TLV_LENGTH_FIELD_MAX_BYTES);

    // tlv_setting_normalize clamps BOTH type_bytes and length_bytes
    tlv_setting_t bad;
    bad.type_bytes = 64;      // > TLV_TYPE_MAX_BYTES
    bad.length_bytes = 16;    // > TLV_LENGTH_FIELD_MAX_BYTES
    tlv_setting_normalize(&bad);
    assert(bad.type_bytes == TLV_TYPE_MAX_BYTES);
    assert(bad.length_bytes == TLV_LENGTH_FIELD_MAX_BYTES);

    // codec must reject an un-normalized out-of-range setting (no overrun / no crash)
    tlv_setting_t oversize;
    oversize.type_bytes = 64;
    oversize.length_bytes = 4;
    TLVMessage tlv;
    tlv.setValue("x", 1);
    char buf[128];
    assert(tlv.packSize(&oversize) < 0);
    assert(tlv.pack(buf, sizeof(buf), &oversize) < 0);
    assert(tlv.unpack(buf, sizeof(buf), &oversize) < 0);
    printf("  width clamp + oversize rejected: OK\n");
}

int main() {
    printf("test_tlv:\n");
    {
        tlv_setting_t s; test_roundtrip(s);          // default 4/4/BE
        s.big_endian = false; test_roundtrip(s);     // 4/4/LE
        s.type_bytes = 2; s.length_bytes = 2; s.big_endian = true; test_roundtrip(s);
        s.type_bytes = 1; s.length_bytes = 4; test_roundtrip(s);
        s.type_bytes = 0; s.length_bytes = 4; test_roundtrip(s); // LV only
        s.type_bytes = 8; s.length_bytes = 4; test_roundtrip(s); // T=8 (hrpc uses this)
    }
    test_type_slicing();
    test_short_buffer();
    test_value_too_big();
    test_width_clamp();
    printf("ALL PASS\n");
    return 0;
}
