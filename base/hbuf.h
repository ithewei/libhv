#ifndef HV_BUF_H_
#define HV_BUF_H_

#include "hdef.h"   // for MAX
#include "hbase.h"  // for HV_ALLOC, HV_FREE

typedef struct hbuf_s {
    char*  base;
    size_t len;

#ifdef __cplusplus
    hbuf_s() {
        base = NULL;
        len  = 0;
    }

    hbuf_s(void* data, size_t len) {
        this->base = (char*)data;
        this->len  = len;
    }
#endif
} hbuf_t;

typedef struct offset_buf_s {
    char*   base;
    size_t  len;
    size_t  offset;
#ifdef __cplusplus
    offset_buf_s() {
        base = NULL;
        len = 0;
        offset = 0;
    }

    offset_buf_s(void* data, size_t len) {
        this->base = (char*)data;
        this->len = len;
        offset = 0;
    }
#endif
} offset_buf_t;

typedef struct fifo_buf_s {
    char*  base;
    size_t len;
    size_t head;
    size_t tail;
#ifdef __cplusplus
    fifo_buf_s() {
        base = NULL;
        len = 0;
        head = tail = 0;
    }

    fifo_buf_s(void* data, size_t len) {
        this->base = (char*)data;
        this->len = len;
        head = tail = 0;
    }
#endif
} fifo_buf_t;

#ifdef __cplusplus
class HBuf : public hbuf_t {
public:
    HBuf() : hbuf_t() {
        cleanup_ = false;
    }
    HBuf(void* data, size_t len) : hbuf_t(data, len) {
        cleanup_ = false;
    }
    HBuf(size_t cap) { resize(cap); }

    virtual ~HBuf() {
        cleanup();
    }

    void*  data() { return base; }
    size_t size() { return len; }

    bool isNull() { return base == NULL || len == 0; }

    void cleanup() {
        if (cleanup_) {
            HV_FREE(base);
            len = 0;
            cleanup_ = false;
        }
    }

    void resize(size_t cap) {
        if (cap == len) return;

        if (base == NULL) {
            HV_ALLOC(base, cap);
        }
        else {
            base = (char*)hv_realloc(base, cap, len);
        }
        len = cap;
        cleanup_ = true;
    }

    void copy(void* data, size_t len) {
        resize(len);
        memcpy(base, data, len);
    }

    void copy(hbuf_t* buf) {
        copy(buf->base, buf->len);
    }

private:
    bool cleanup_;
};

// VL: Variable-Length
class HVLBuf : public HBuf {
public:
    HVLBuf() : HBuf() {_offset = _size = 0;}
    HVLBuf(void* data, size_t len) : HBuf(data, len) {_offset = 0; _size = len;}
    HVLBuf(size_t cap) : HBuf(cap) {_offset = _size = 0;}
    virtual ~HVLBuf() {}

    char* data() { return base + _offset; }
    size_t size() { return _size; }

    void push_front(void* ptr, size_t len) {
        if (len > this->len - _size) {
            size_t newsize = MAX(this->len, len)*2;
            resize(newsize);
        }

        if (_offset < len) {
            // move => end
            memmove(base+this->len-_size, data(), _size);
            _offset = this->len-_size;
        }

        memcpy(data()-len, ptr, len);
        _offset -= len;
        _size += len;
    }

    void push_back(void* ptr, size_t len) {
        if (len > this->len - _size) {
            size_t newsize = MAX(this->len, len)*2;
            resize(newsize);
        }
        else if (len > this->len - _offset - _size) {
            // move => start
            memmove(base, data(), _size);
            _offset = 0;
        }
        memcpy(data()+_size, ptr, len);
        _size += len;
    }

    void pop_front(void* ptr, size_t len) {
        if (len <= _size) {
            if (ptr) {
                memcpy(ptr, data(), len);
            }
            _offset += len;
            if (_offset >= this->len) _offset = 0;
            _size   -= len;
        }
    }

    void pop_back(void* ptr, size_t len) {
        if (len <= _size) {
            if (ptr) {
                memcpy(ptr, data()+_size-len, len);
            }
            _size -= len;
        }
    }

    void clear() {
        _offset = _size = 0;
    }

    void prepend(void* ptr, size_t len) {
        push_front(ptr, len);
    }

    void append(void* ptr, size_t len) {
        push_back(ptr, len);
    }

    void insert(void* ptr, size_t len) {
        push_back(ptr, len);
    }

    void remove(size_t len) {
        pop_front(NULL, len);
    }

private:
    size_t _offset;
    size_t _size;
};

class HRingBuf : public HBuf {
public:
    HRingBuf() : HBuf() {_head = _tail = _size = 0;}
    HRingBuf(size_t cap) : HBuf(cap) {_head = _tail = _size = 0;}
    virtual ~HRingBuf() {}

    char* alloc(size_t len) {
        char* ret = NULL;
        if (_head < _tail || _size == 0) {
            // [_tail, this->len) && [0, _head)
            if (this->len - _tail >= len) {
                ret = base + _tail;
                _tail += len;
                if (_tail == this->len) _tail = 0;
            }
            else if (_head >= len) {
                ret = base;
                _tail = len;
            }
        }
        else {
            // [_tail, _head)
            if (_head - _tail >= len) {
                ret = base + _tail;
                _tail += len;
            }
        }
        _size += ret ? len : 0;
        return ret;
    }

    void free(size_t len) {
        _size -= len;
        if (len <= this->len - _head) {
            _head += len;
            if (_head == this->len) _head = 0;
        }
        else {
            _head = len;
        }
    }

    void clear() {_head = _tail = _size = 0;}

    size_t size() {return _size;}

private:
    size_t _head;
    size_t _tail;
    size_t _size;
};
#endif

#endif // HV_BUF_H_
