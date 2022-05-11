#ifndef __HIO_H__
#define __HIO_H__

#include "HForwardDecls.h"
#include "hv/hbuf.h"

#include <functional>

namespace hvpp {

    class HIo {
    public:
        HIo();
        ~HIo();
        HIo(const HIo&) = delete;
        HIo& operator =(const HIo&) = delete;
        using Callback = std::function<void(HIo&)>;
        using ReadCallback = std::function<void(HIo&, HBuf &buf)>;
        using WriteCallback = std::function<void(HIo&, const HBuf &buf)>;
        Callback onAccept = nullptr;
        Callback onConnect = nullptr;
        Callback onClose = nullptr;
        ReadCallback onRead = nullptr;
        WriteCallback onWrite = nullptr;

        int32_t StartRead();
        int32_t Write(const char *s, size_t len = 0);
        int32_t Write(const void *d, size_t len);

        HLoop &Loop();
        int32_t Attach(HLoop &loop);
        int32_t Detach();

        struct Implement;

    private:
        friend HLoop;
        Implement *_pimpl = nullptr;
    };
}

#endif // __HIO_H__
