#ifndef HV_EVENT_HPP_
#define HV_EVENT_HPP_

#include <functional>
#include <memory>

#include "hloop.h"
#include "hdns.h"
#include "hsocket.h"

namespace hv {

struct Event;
struct Timer;
struct DnsQuery;

typedef uint64_t            TimerID;
#define INVALID_TIMER_ID    ((hv::TimerID)-1)

// DNS query id: a use-after-free-proof handle for EventLoop::resolveDns().
// Mirrors TimerID: the EventLoop keeps a DnsID -> DnsQuery map, so a stale id
// (completed / cancelled query) simply misses the map and cancelDns() no-ops.
typedef uint64_t            DnsID;
#define INVALID_DNS_ID      ((hv::DnsID)0)

typedef std::function<void(Event*)>     EventCallback;
typedef std::function<void(TimerID)>    TimerCallback;
// DNS resolve result: status==0 on success; addrs are A/AAAA (IPv4 first).
typedef std::function<void(int status, int naddrs, const sockaddr_u* addrs)> DnsCallback;

struct Event {
    hevent_t        event;
    EventCallback   cb;

    Event(EventCallback cb = NULL) {
        memset(&event, 0, sizeof(hevent_t));
        this->cb = std::move(cb);
    }
};

struct Timer {
    htimer_t*       timer;
    TimerCallback   cb;
    uint32_t        repeat;

    Timer(htimer_t* timer = NULL, TimerCallback cb = NULL, uint32_t repeat = INFINITE) {
        this->timer = timer;
        this->cb = std::move(cb);
        this->repeat = repeat;
    }
};

// Tracks one in-flight EventLoop::resolveDns() query. The EventLoop owns the
// DnsID -> DnsQuery map; the underlying hdns_t* is owned by the C resolver and
// carries this DnsID in its event_id so completion can erase the map entry.
struct DnsQuery {
    hdns_t*         query;
    DnsCallback     cb;

    DnsQuery(hdns_t* query = NULL, DnsCallback cb = NULL) {
        this->query = query;
        this->cb = std::move(cb);
    }
};

typedef std::shared_ptr<Event> EventPtr;
typedef std::shared_ptr<Timer> TimerPtr;
typedef std::shared_ptr<DnsQuery> DnsQueryPtr;

}

#endif // HV_EVENT_HPP_
