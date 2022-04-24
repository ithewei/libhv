#ifndef __HEVENT_H__
#define __HEVENT_H__

#include "HForwardDecls.h"

namespace hvpp {
    class HEvent {
    public:
        HEvent();
        ~HEvent();
    private:
        friend HLoop;
        struct Implement;
        Implement *_pimpl = nullptr;
    };
}

#endif // __HEVENT_H__
