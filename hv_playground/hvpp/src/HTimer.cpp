#include "HTimerImplement.h"
#include "HLoop.h"

namespace hvpp {
    HTimer::HTimer() : _pimpl(new Implement()) {
        _pimpl->owner = this;
    }
    HTimer::~HTimer() {
        delete _pimpl;
    }
}
