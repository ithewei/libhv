#ifndef H_FRAME_H
#define H_FRAME_H

#include <deque>

#include "hbuf.h"
#include "hmutex.h"

typedef struct hframe_s{
    hbuf_t buf;
    int w;
    int h;
    int type;
    int bpp;
    uint64 ts;
    void* userdata;
    hframe_s(){
        w = h = type = bpp = ts = 0;
        userdata = NULL;
    }

    bool isNull(){
        return w == 0 || h == 0 || buf.isNull();
    }

    // deep copy
    void copy(const hframe_s& rhs){
        this->w = rhs.w;
        this->h = rhs.h;
        this->type = rhs.type;
        this->bpp  = rhs.bpp;
        this->ts   = rhs.ts;
        this->userdata = rhs.userdata;
        if (this->buf.isNull() || this->buf.len != rhs.buf.len){
            this->buf.init(rhs.buf.len);
        }
        memcpy(this->buf.base, rhs.buf.base, rhs.buf.len);
    }
}HFrame;

typedef struct frame_info_s{
    int w;
    int h;
    int type;
    int bpp;
}FrameInfo;

typedef struct frame_stats_s{
    int push_cnt;
    int pop_cnt;

    int push_ok_cnt;
    int pop_ok_cnt;

    frame_stats_s(){
        push_cnt = pop_cnt = push_ok_cnt = pop_ok_cnt = 0;
    }
}FrameStats;

#define DEFAULT_FRAME_CACHENUM  10

class HFrameBuf : public HRingBuf {
public:
    enum CacheFullPolicy{
        SQUEEZE,
        DISCARD,       
    }policy;
    
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

#endif // H_FRAME_H
