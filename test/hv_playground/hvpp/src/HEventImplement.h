#ifndef __HEVENTIMPLEMENT_H__
#define __HEVENTIMPLEMENT_H__

#include "HEvent.h"

#include "hv/hloop.h"

namespace hvpp {
    struct HEvent::Implement {
        hevent_t event;
    };
} // namespace hvpp

#endif // __HEVENTIMPLEMENT_H__
