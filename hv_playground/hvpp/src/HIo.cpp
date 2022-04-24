#include "HIoImplement.h"

namespace hvpp {
    HIo::HIo() : _pimpl(new HIo::Implement()) {
        _pimpl->owner = this;
    }

    HIo::~HIo() {
        delete _pimpl;
    }

    int32_t HIo::StartRead() {
        return hio_read_start(_pimpl->h_io);
    }

    int32_t HIo::Write(const char *s, size_t len) {
        if (len <= 0) {
            len = strlen(s);
        }
        return hio_write(_pimpl->h_io, s, len);
    }

    int32_t HIo::Write(const void *d, size_t len) {
        return hio_write(_pimpl->h_io, d, len);
    }

    HLoop &HIo::Loop() {
        HLoop *pLoop = nullptr;
        auto loop = hevent_loop(_pimpl->h_io);
        if (loop) {
            auto pLoopImpl = reinterpret_cast<HLoop::Implement*>(hloop_userdata(loop));
            if (pLoopImpl) {
                pLoop = pLoopImpl->owner;
            }
        }
        return pLoop ? *pLoop : HLoop::NullLoop();
    }

    int32_t HIo::Attach(HLoop &loop) {
        if (&Loop() != &loop) {
            Detach();
            hio_attach(loop._pimpl->loop, _pimpl->h_io);
        }
        return 0;
    }

    int32_t HIo::Detach() {
        hio_detach(_pimpl->h_io);
        hevent_loop(_pimpl->h_io) = nullptr;
        return 0;
    }
} // namespace hvpp

