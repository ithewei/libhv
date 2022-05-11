#ifndef __HIOIMPLEMENT_H__
#define __HIOIMPLEMENT_H__

#include "HIo.h"
#include "HLoopImplement.h"

#include "hv/hloop.h"
#include "hv/hbuf.h"

namespace hvpp {
    struct HIo::Implement {
        static void s_accept_cb(hio_t *hio) {
            auto pio = reinterpret_cast<hvpp::HIo *>(hevent_userdata(hio));
            auto loop = reinterpret_cast<hvpp::HLoop::Implement *>(hloop_userdata(hevent_loop(hio)));
            auto &newIo = loop->ios[hio];
            newIo._pimpl->SetIo(hio);
            if (pio->onAccept) {
                pio->onAccept(newIo);
            }
            loop->ios[hio].StartRead();
        }

        static void s_connect_cb(hio_t *hio) {
            auto pio = reinterpret_cast<hvpp::HIo *>(hevent_userdata(hio));
            if (pio->onConnect) {
                pio->onConnect(*pio);
            }
        }

        static void s_close_cb(hio_t *hio) {
            auto pio = reinterpret_cast<hvpp::HIo *>(hevent_userdata(hio));
            if (pio->onClose) {
                pio->onClose(*pio);
            }
        }

        static void s_read_cb(hio_t *hio, void *hbuf, int readbytes) {
            auto pio = reinterpret_cast<hvpp::HIo *>(hevent_userdata(hio));
            if (pio->onRead) {
                auto buf = HBuf(hbuf, readbytes);
                pio->onRead(*pio, buf);
            }
        }

        static void s_write_cb(hio_t *hio, const void *hbuf, int writebytes) {
            auto pio = reinterpret_cast<hvpp::HIo *>(hevent_userdata(hio));
            if (pio->onWrite) {
                auto buf = HBuf(const_cast<void *>(hbuf), writebytes);
                pio->onWrite(*pio, buf);
            }
        }

        ~Implement() = default;
        void SetIo(hio_t *h_io) {
            this->h_io = h_io;
            if (h_io) {
                hevent_set_userdata(h_io, owner);
                hio_setcb_accept(h_io, s_accept_cb);
                hio_setcb_connect(h_io, s_connect_cb);
                hio_setcb_close(h_io, s_close_cb);
                hio_setcb_read(h_io, s_read_cb);
                hio_setcb_write(h_io, s_write_cb);
            }
        }
        HIo *owner = nullptr;
        hio_t *h_io = nullptr;
    };
}

#endif // __HIOIMPLEMENT_H__
