#ifndef HV_TLV_CLIENT_HPP_
#define HV_TLV_CLIENT_HPP_

#include "TcpClient.h"
#include "TLVChannel.h"

namespace hv {

// TCP client speaking the TLV frame protocol.
// The T/L widths come from a tlv_setting_t (default T=4/L=4/big-endian); the
// matching unpacker is installed so onmessage receives one whole frame as a TLV.
class TLVClient : public TcpClientTmpl<TLVChannel> {
public:
    TLVClient(EventLoopPtr loop = NULL) : TcpClientTmpl<TLVChannel>(loop) {
        setTLV(&tlv_setting_);
        // Intercept the low-level onMessage: each buf is one whole TLV frame.
        onMessage = [this](const TLVChannelPtr& channel, Buffer* buf) {
            TLVMessage tlv;
            if (tlv.unpack(buf->data(), buf->size(), &tlv_setting_) < 0) return;
            if (onmessage) onmessage(channel, tlv);
        };
    }
    virtual ~TLVClient() {
        // Stop the loop thread before this object's members (tlv_setting_,
        // onmessage) are destroyed. The base ~TcpClientTmpl also calls stop(),
        // but that runs after our members are gone -- an in-flight onMessage
        // would then touch freed state (UAF).
        // Separately, detach the channel close callback so ~Channel -> close()
        // doesn't fire onclose during teardown (mirrors WebSocketClient).
        if (channel) {
            channel->onclose = NULL;
        }
        stop();
    }

    // NOTE: call before start(). Reconfigures both the TLV codec and the unpacker.
    // length_bytes is clamped (framing unpacker is 32-bit) so codec and framing agree.
    void setTLV(const tlv_setting_t* setting) {
        tlv_setting_ = *setting;
        tlv_setting_normalize(&tlv_setting_);
        // The base setUnpack copies the value into its own storage, so a local is fine.
        unpack_setting_t unpack;
        tlv_unpack_setting(&unpack, &tlv_setting_);
        setUnpack(&unpack);
    }
    const tlv_setting_t& tlv() const { return tlv_setting_; }

    int sendTLV(const void* type, int typelen, const void* data, int len) {
        if (!isConnected()) return -1;
        return channel->sendTLV(type, typelen, data, len, &tlv_setting_);
    }
    int sendTLV(const TLVMessage& tlv) {
        if (!isConnected()) return -1;
        return channel->sendTLV(tlv, &tlv_setting_);
    }

public:
    // High-level per-frame callback (a whole TLV).
    std::function<void(const TLVChannelPtr&, const TLVMessage&)> onmessage;

protected:
    tlv_setting_t    tlv_setting_;
};

} // namespace hv

#endif // HV_TLV_CLIENT_HPP_
