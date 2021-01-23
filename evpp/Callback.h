#ifndef HV_CALLBACK_HPP_
#define HV_CALLBACK_HPP_

#include <functional>

#include "Buffer.h"
#include "Channel.h"

namespace hv {

typedef std::function<void(const SocketChannelPtr&)>            ConnectionCallback;
typedef std::function<void(const SocketChannelPtr&, Buffer*)>   MessageCallback;
typedef std::function<void(const SocketChannelPtr&, Buffer*)>   WriteCompleteCallback;

}

#endif // HV_CALLBACK_HPP_
