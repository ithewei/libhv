#ifndef __HLOOP_H__
#define __HLOOP_H__

#include "HIo.h"
#include "HTimer.h"

#include <cstdint>

namespace hvpp {
    enum HLoopFlag : uint32_t {
        None = 0,
        RunOnce = 1,
        QuitWhenNoActiveEvents = 2,
    };

    enum class HLoopStatus : uint32_t {
        Stop,
        Running,
        Pause
    };


    class HLoop {
    public:
        explicit HLoop(HLoopFlag flag = HLoopFlag::None);
        ~HLoop();

        int32_t Run();
        int32_t Stop();
        int32_t Pause();
        int32_t Resume();
        int32_t Wakeup();
        HLoopStatus Status() const;
        void UpdateTime();
        uint64_t Now() const;
        uint64_t NowMs() const;
        uint64_t NowUs() const;
        long Pid() const;
        long Tid() const;
        void SetUserData(void *userData);
        void* UserData() const;
        void PostEvent(HEvent *pEvt);

        HIo& CreateIo(int32_t fd);
        HIo& CreateTcpServer(const char *host, int port, HIo::Callback acceptCb);

        HTimer &AddTimer(HTimer::Callback timeoutCb, int32_t timeout, uint32_t repeat=INFINITE);
        HTimer &AddPeriodTimer(HTimer::Callback timeoutCb, int8_t minut =0,  int8_t hour = -1, 
            int8_t day = -1, int8_t week = -1, int8_t month = -1, uint32_t repeat = INFINITE);

        static HLoop &NullLoop();
        struct Implement;

    private:
        friend HIo;
        Implement *_pimpl;
    };
}

#endif // __HLOOP_H__
