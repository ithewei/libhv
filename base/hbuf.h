#ifndef HV_BUF_H_
#define HV_BUF_H_

/*
 * @功能：此头文件提供了一些常用的buffer
 *
 */

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

// offset_buf_t 多了一个offset偏移量成员变量，
// 通常用于一次未操作完，记录下一次操作的起点
typedef struct offset_buf_s {
    char*   base;
    size_t  len;
    size_t  offset;
#ifdef __cplusplus
    offset_buf_s() {
        base = NULL;
        len = offset = 0;
    }

    offset_buf_s(void* data, size_t len) {
        this->base = (char*)data;
        this->len = len;
    }
#endif
} offset_buf_t;

#ifdef __cplusplus
class HBuf : public hbuf_t {
public:
    HBuf() : hbuf_t() {
        cleanup_ = false;
    }
    // 浅拷贝构造函数
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
            base = (char*)safe_realloc(base, cap, len);
        }
        len = cap;
        cleanup_ = true;
    }

    // 深拷贝
    void copy(void* data, size_t len) {
        resize(len);
        memcpy(base, data, len);
    }

    void copy(hbuf_t* buf) {
        copy(buf->base, buf->len);
    }

private:
    bool cleanup_; // cleanup_变量用来记录buf是浅拷贝还是深拷贝，如果是深拷贝，析构时需释放掉分配内存
};

// 可变长buffer类型，支持push_front/push_back/pop_front/pop_back操作
// VL: Variable-Length
class HVLBuf : public HBuf {
public:
    HVLBuf() : HBuf() {_offset = _size = 0;}
    HVLBuf(void* data, size_t len) : HBuf(data, len) {_offset = 0; _size = len;}
    HVLBuf(size_t cap) : HBuf(cap) {_offset = _size = 0;}
    virtual ~HVLBuf() {}

    // 返回当前起点
    char* data() { return base + _offset; }
    // 返回有效长度
    size_t size() { return _size; }

    void push_front(void* ptr, size_t len) {
        // 如果插入长度超过了剩余空间，则重新分配足够空间
        if (len > this->len - _size) {
            size_t newsize = MAX(this->len, len)*2;
            base = (char*)safe_realloc(base, newsize, this->len);
            this->len = newsize;
        }

        // 如果前面空间不足，则需要先整体后移
        if (_offset < len) {
            // move => end
            memmove(base+this->len-_size, data(), _size);
            _offset = this->len-_size;
        }

        // 插入到当前起点的前面
        memcpy(data()-len, ptr, len);
        // 记录新的起点位置
        _offset -= len;
        // 有效长度增加
        _size += len;
    }

    void push_back(void* ptr, size_t len) {
        // 如果插入长度超过了剩余空间，则重新分配足够空间
        if (len > this->len - _size) {
            size_t newsize = MAX(this->len, len)*2;
            base = (char*)safe_realloc(base, newsize, this->len);
            this->len = newsize;
        }
        // 如果后面空间不足，则需要先整体前移
        else if (len > this->len - _offset - _size) {
            // move => start
            memmove(base, data(), _size);
            _offset = 0;
        }
        // 插入到后面
        memcpy(data()+_size, ptr, len);
        // 起点位置不变，有效长度增加
        _size += len;
    }

    void pop_front(void* ptr, size_t len) {
        if (len <= _size) {
            // 将数据从开始位置拷贝出来
            if (ptr) {
                memcpy(ptr, data(), len);
            }
            // 起点位置后移
            _offset += len;
            // 如果起点位置已经到了结尾，则重置为0
            if (_offset >= this->len) _offset = 0;
            // 有效长度减少
            _size   -= len;
        }
    }

    void pop_back(void* ptr, size_t len) {
        if (len <= _size) {
            // 将数据从尾部拷贝出来
            if (ptr) {
                memcpy(ptr, data()+_size-len, len);
            }
            // 起点位置不变，有效长度减少
            _size -= len;
        }
    }

    void clear() {
        // 清除操作：将起点位置和有效长度重置为0
        _offset = _size = 0;
    }

    // 一些别名函数
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
    size_t _offset; // _offet用来记录当前起点的偏移量
    size_t _size; // _size用来记录有效长度
};

// 环形buffer：有序从环形buffer中分配与释放内存，避免频繁调用系统调用
class HRingBuf : public HBuf {
public:
    HRingBuf() : HBuf() {_head = _tail = _size = 0;}
    HRingBuf(size_t cap) : HBuf(cap) {_head = _tail = _size = 0;}
    virtual ~HRingBuf() {}

    char* alloc(size_t len) {
        char* ret = NULL;
        // 如果头指针在尾指针前面或者已用长度等于0
        if (_head < _tail || _size == 0) {
            // [_tail, this->len) && [0, _head)
            // 如果尾指针后面剩余空间足够，则从尾指针后开始分配空间
            if (this->len - _tail >= len) {
                ret = base + _tail;
                _tail += len;
                if (_tail == this->len) _tail = 0;
            }
            // 如果头指针前面剩余空间足够，则从0开始分配空间
            else if (_head >= len) {
                ret = base;
                _tail = len;
            }
        }
        else {
            // [_tail, _head)
            // 如果尾指针到头指针间的空间足够，则从尾指针后开始分配空间
            if (_head - _tail >= len) {
                ret = base + _tail;
                _tail += len;
            }
        }
        // 分配到了空间，已用长度增加
        _size += ret ? len : 0;
        return ret;
    }

    void free(size_t len) {
        // 已用长度减少
        _size -= len;
        // 如果释放的长度小于头指针后面的长度，头指针后移，
        if (len <= this->len - _head) {
            _head += len;
            if (_head == this->len) _head = 0;
        }
        // 否则说明头指针已抵尾部，这块释放的内存是从0开始分配的，头指针置为len即可
        else {
            _head = len;
        }
    }

    void clear() {_head = _tail = _size = 0;}

    size_t size() {return _size;}

private:
    size_t _head; // 头指针，用来记录读位置
    size_t _tail; // 尾指针，用来记录写位置
    size_t _size; // 用来记录已用长度
};
#endif

#endif // HV_BUF_H_
