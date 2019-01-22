#ifndef HW_FRAME_H_
#define HW_FRAME_H_

#include <deque>

#include "hbuf.h"
#include "hmutex.h"

class HFrame {
public:
    hbuf_t buf;
    int w;
    int h;
    int bpp;
    int type;
    uint64 ts;
    int64 useridx;
    void* userdata;
    HFrame() {
        w = h = bpp = type = ts = 0;
        useridx = -1;
        userdata = NULL;
    }

    ~HFrame() {
    }

    void copy(const HFrame& rhs) {
        w = rhs.w;
        h = rhs.h;
        bpp = rhs.bpp;
        type = rhs.type;
        ts = rhs.ts;
        useridx = rhs.useridx;
        userdata = rhs.userdata;
        if (buf.isNull() || buf.len != rhs.buf.len) {
            buf.init(rhs.buf.len);
        }
        memcpy(buf.base, rhs.buf.base, rhs.buf.len);
    }

    bool isNull() {
        return w == 0 || h == 0 || buf.isNull();
    }
};

typedef struct frame_info_s {
    int w;
    int h;
    int type;
    int bpp;
} FrameInfo;

typedef struct frame_stats_s {
    int push_cnt;
    int pop_cnt;

    int push_ok_cnt;
    int pop_ok_cnt;

    frame_stats_s() {
        push_cnt = pop_cnt = push_ok_cnt = pop_ok_cnt = 0;
    }
} FrameStats;

#define DEFAULT_FRAME_CACHENUM  10

class HFrameBuf : public HRingBuf {
 public:
    enum CacheFullPolicy {
        SQUEEZE,
        DISCARD,
    } policy;

    HFrameBuf() : HRingBuf() {
        cache_num = DEFAULT_FRAME_CACHENUM;
        policy = SQUEEZE;
    }

    void setCache(int num) {cache_num = num;}
    void setPolicy(CacheFullPolicy policy) {this->policy = policy;}

    int push(HFrame* pFrame);
    int pop(HFrame* pFrame);

    int         cache_num;
    FrameStats  frame_stats;
    FrameInfo   frame_info;
    std::deque<HFrame> frames;
    std::mutex         mutex;
};

#endif  // HW_FRAME_H_
