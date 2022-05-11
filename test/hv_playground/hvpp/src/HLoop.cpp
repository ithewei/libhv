#include "HLoopImplement.h"
#include "HEventImplement.h"
#include "HIoImplement.h"
#include "HTimerImplement.h"

#include <utility> // for std::move

namespace hvpp {

    HLoop::HLoop(HLoopFlag flag) : _pimpl(new HLoop::Implement(flag)) {
        _pimpl->owner = this;
    }
    HLoop::~HLoop() {
        delete _pimpl;
    }

    int32_t HLoop::Run() {
        return hloop_run(_pimpl->loop);
    }

    int32_t HLoop::Stop() {
        return hloop_stop(_pimpl->loop);
    }

    int32_t HLoop::Pause() {
        return hloop_pause(_pimpl->loop);
    }

    int32_t HLoop::Resume() {
        return hloop_resume(_pimpl->loop);
    }

    int32_t HLoop::Wakeup() {
        return hloop_wakeup(_pimpl->loop);
    }

    HLoopStatus HLoop::Status() const {
        return static_cast<HLoopStatus>(hloop_status(_pimpl->loop));
    }

    void HLoop::UpdateTime() {
        return hloop_update_time(_pimpl->loop);
    }

    uint64_t HLoop::Now() const {
        return hloop_now(_pimpl->loop);
    }

    uint64_t HLoop::NowMs() const {
        return hloop_now_ms(_pimpl->loop);
    }

    uint64_t HLoop::NowUs() const {
        return hloop_now_us(_pimpl->loop);
    }

    long HLoop::Pid() const {
        return hloop_pid(_pimpl->loop);
    }

    long HLoop::Tid() const {
        return hloop_tid(_pimpl->loop);
    }

    void HLoop::SetUserData(void *userData) {
        _pimpl->userData = userData;
    }

    void *HLoop::UserData() const {
        return _pimpl->userData;
    }

    void HLoop::PostEvent(HEvent *pEvt) {
        return hloop_post_event(_pimpl->loop, &pEvt->_pimpl->event);
    }

    HIo& HLoop::CreateIo(int32_t fd) {
        auto h_io = hio_get(_pimpl->loop, fd);
        auto &io = _pimpl->ios[h_io];
        hevent_set_userdata(h_io, &io);
        io._pimpl->h_io = h_io;
        return io;
    }

    HIo& HLoop::CreateTcpServer(const char *host, int port, HIo::Callback acceptCb) {
        auto h_io = hloop_create_tcp_server(_pimpl->loop, host, port, nullptr);
        auto &io = _pimpl->ios[h_io];
        io._pimpl->SetIo(h_io);
        io.onAccept = acceptCb;
        return io;
    }

    HTimer &HLoop::AddTimer(HTimer::Callback timeoutCb, int32_t timeout, uint32_t repeat) {
        auto h_timer = htimer_add(_pimpl->loop, HTimer::Implement::timeoutCb, timeout, repeat);
        auto &timer = _pimpl->timers[h_timer];
        timer._pimpl->SetTimer(h_timer);
        timer.onTimeout = timeoutCb;
        return timer;
    }

    HTimer &HLoop::AddPeriodTimer(HTimer::Callback timeoutCb, int8_t minut, int8_t hour,
        int8_t day, int8_t week, int8_t month, uint32_t repeat) {
        auto h_timer = htimer_add_period(_pimpl->loop, HTimer::Implement::timeoutCb, 
            minut, hour, day, week,month, repeat);
        auto &timer = _pimpl->timers[h_timer];
        timer._pimpl->SetTimer(h_timer);
        timer.onTimeout = timeoutCb;
        return timer;
    }

    HLoop &HLoop::NullLoop() {
        static HLoop nullLoop;
        if (nullLoop._pimpl->loop) {
            hloop_free(&nullLoop._pimpl->loop);
            nullLoop._pimpl->loop = nullptr;
        }
        return nullLoop;
    }
} // namespace hvpp

