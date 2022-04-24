#ifndef __HTIMER_H__
#define __HTIMER_H__

#include "HForwardDecls.h"

#include <functional>

namespace hvpp {
    class HTimer {
    public:
        using Callback = std::function<void(HTimer&)>;
        HTimer();
        ~HTimer();
        HTimer(const HTimer &) = delete;
        HTimer& operator=(const HTimer &) = delete;

        Callback onTimeout = nullptr;

        struct Implement;

    private:
        friend HLoop;
        Implement *_pimpl = nullptr;
    };
} // namespace hvpp

#endif // __HTIMER_H__