#ifndef HV_TLV_CHANNEL_HPP_
#define HV_TLV_CHANNEL_HPP_

#include "Channel.h"
#include "TLVMessage.h"

namespace hv {

// A SocketChannel that frames outbound data as TLV.
// Inbound framing is done by the underlying UNPACK_BY_LENGTH_FIELD unpacker
// (configured by TLVClient/TLVServer via setUnpack), so each onread Buffer is
// already one whole TLV frame; the client/server parses it and delivers a TLV.
class TLVChannel : public SocketChannel {
public:
    TLVChannel(hio_t* io) : SocketChannel(io) {}
    virtual ~TLVChannel() {}

    // Send one TLV frame. type/typelen fill the Type field; data/len the Value.
    int sendTLV(const void* type, int typelen, const void* data, int len, const tlv_setting_t* setting) {
        TLVMessage tlv;
        tlv.setType(type, typelen);
        tlv.setValue(data, len);
        return sendTLV(tlv, setting);
    }

    int sendTLV(const TLVMessage& tlv, const tlv_setting_t* setting) {
        int packlen = tlv.packSize(setting);
        if (packlen < 0) return -1;   // value too large for length_bytes
        std::string buf;
        buf.resize(packlen);
        if (tlv.pack(&buf[0], packlen, setting) < 0) return -1;
        return write(buf.data(), packlen);
    }
};

typedef std::shared_ptr<TLVChannel> TLVChannelPtr;

} // namespace hv

#endif // HV_TLV_CHANNEL_HPP_
