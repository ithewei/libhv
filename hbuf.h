#ifndef H_BUF_H
#define H_BUF_H

#include "hdef.h"
#include <stdlib.h>
#include <string.h>
#include <mutex>

typedef struct hbuf_s{
    uint8* base;
    size_t len;

    hbuf_s(){
        base = NULL;
        len  = 0;
    }

    hbuf_s(uint8* base, size_t len){
        this->base = base;
        this->len  = len;
    }

    void init(size_t cap){
        if (cap == len) return;

        if (!base){
            base = (uint8*)malloc(cap);
            memset(base, 0, cap);
        }else{
            base = (uint8*)realloc(base, cap);
        }
        len = cap;
    }

    void resize(size_t cap){
        init(cap);
    }

    void cleanup(){
        SAFE_FREE(base);
        len = 0;
    }

    bool isNull(){
        return base == NULL || len == 0;
    }
}hbuf_t;

class HBuf : public hbuf_t{
public:
    HBuf() : hbuf_t(){}
    HBuf(size_t cap) {init(cap);}
    HBuf(void* data, size_t len){
        init(len);
        memcpy(base, data, len);
    }
    virtual ~HBuf() {cleanup();}

    std::mutex mutex; // used in multi-thread
};

// VL: Variable-Length
class HVLBuf : public HBuf{
public:
    HVLBuf() : HBuf() {_offset = _size = 0;}
    HVLBuf(size_t cap) : HBuf(cap) {_offset = _size = 0;}
    HVLBuf(void* data, size_t len) : HBuf(data, len) {_offset = 0; _size = len;}
    virtual ~HVLBuf() {}

    uint8* data() {return base+_offset;}
    size_t size() {return _size;}

    void push_front(void* ptr, size_t len){
        if (len > this->len - _size){
            this->len = MAX(this->len, len)*2;
            base = (uint8*)realloc(base, this->len);
        }
        
        if (_offset < len){
            // move => end
            memmove(base+this->len-_size, data(), _size);
            _offset = this->len-_size;
        }

        memcpy(data()-len, ptr, len);
        _offset -= len;
        _size += len;
    }

    void push_back(void* ptr, size_t len){
        if (len > this->len - _size){
            this->len = MAX(this->len, len)*2;
            base = (uint8*)realloc(base, this->len);
        }else if (len > this->len - _offset - _size){
            // move => start
            memmove(base, data(), _size);
            _offset = 0;
        }
        memcpy(data()+_size, ptr, len);
        _size += len;
    }

    void pop_front(void* ptr, size_t len){
        if (len <= _size){
            if (ptr){
                memcpy(ptr, data(), len);
            }
            _offset += len;
            if (_offset >= len) _offset = 0;
            _size   -= len;
        }
    }

    void pop_back(void* ptr, size_t len){
        if (len <= _size){
            if (ptr){
                memcpy(ptr, data()+_size-len, len);
            }
            _size -= len;
        }
    }

    void clear(){
        _offset = _size = 0;
    }

    void prepend(void* ptr, size_t len){
        push_front(ptr, len);
    }

    void append(void* ptr, size_t len){
        push_back(ptr, len);
    }

    void insert(void* ptr, size_t len){
        push_back(ptr, len);
    }

    void remove(size_t len){
        pop_front(NULL, len);
    }

private:
    size_t _offset;
    size_t _size;
};

class HRingBuf : public HBuf{
public:
    HRingBuf() : HBuf() {_head = _tail = _size = 0;}
    HRingBuf(size_t cap) : HBuf(cap) {_head = _tail = _size = 0;}

    uint8* alloc(size_t len){
        uint8* ret = NULL;
        if (_head < _tail || _size == 0){
            // [_tail, this->len) && [0, _head)
            if (this->len - _tail >= len){
                ret = base + _tail;
                _tail += len;
                if (_tail == this->len) _tail = 0;
            }else if(_head >= len){
                ret = base;
                _tail = len;
            }
        }else{
            // [_tail, _head)
            if (_head - _tail >= len){
                ret = base + _tail;
                _tail += len;
            }
        }
        _size += ret ? len : 0;
        return ret;
    }

    void   free(size_t len){
        _size -= len;
        if (len <= this->len - _head){
            _head += len;
            if (_head == this->len) _head = 0;
        }else{
            _head = len;
        }
    }

    size_t size() {return _size;}

private:
    size_t _head;
    size_t _tail;
    size_t _size;
};

#endif // H_BUF_H
