#include "HEventImplement.h"

namespace hvpp {
    HEvent::HEvent() : _pimpl(new HEvent::Implement()) {}
    HEvent::~HEvent() {
        delete _pimpl;
    }
} // namespace hvpp
