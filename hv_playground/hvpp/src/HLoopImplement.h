#ifndef __HLOOPIMPLEMENT_H__
#define __HLOOPIMPLEMENT_H__

#include "HLoop.h"

#include "hv/hloop.h"
#include <unordered_map>

namespace hvpp {
    struct HLoop::Implement {
        Implement(HLoopFlag flag) : loop(nullptr) {
            auto hflag = 0;
            if (flag & HLoopFlag::RunOnce) {
                hflag |= HLOOP_FLAG_RUN_ONCE;
            }
            if (flag & HLoopFlag::QuitWhenNoActiveEvents) {
                hflag |= HLOOP_FLAG_QUIT_WHEN_NO_ACTIVE_EVENTS;
            }
            loop = hloop_new(static_cast<int>(hflag));
            hloop_set_userdata(loop, this);
        }

        ~Implement() {
            hloop_free(&loop);
        }

        hloop_t *loop = nullptr;
        void *userData = nullptr;
        HLoop *owner = nullptr;
        std::unordered_map<hio_t *, HIo> ios;
        std::unordered_map<htimer_t *, HTimer> timers;
    };
}

#endif // __HLOOPIMPLEMENT_H__
