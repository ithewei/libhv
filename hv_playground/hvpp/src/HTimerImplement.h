#include "HTimer.h"
#include "hv/hloop.h"

namespace hvpp {
    struct HTimer::Implement {
        static void timeoutCb(htimer_t *timer) { 
            auto htimer = reinterpret_cast<HTimer *>(hevent_userdata(timer));
            if (htimer->onTimeout) {
                htimer->onTimeout(*htimer);
            }
        }

        void SetTimer(htimer_t *timer) { 
            h_timer = timer;
            hevent_set_userdata(h_timer, owner);
        }
        htimer_t *h_timer = nullptr;
        HTimer *owner = nullptr;
    };
}

