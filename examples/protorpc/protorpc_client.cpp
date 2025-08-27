/*
 * proto rpc client
 *
 * @build   make protorpc
 * @server  bin/protorpc_server 1234
 * @client  bin/protorpc_client 127.0.0.1 1234 add 1 2
 *
 */

#include "TcpClient.h"

#include <atomic>
#include <mutex>
#include <future>

using namespace hv;

#include "protorpc.h"
#include "generated/base.pb.h"
#include "generated/calc.pb.h"
#include "generated/login.pb.h"

// valgrind --leak-check=full --show-leak-kinds=all
class ProtobufRAII {
public:
    ProtobufRAII() {
    }
    ~ProtobufRAII() {
        google::protobuf::ShutdownProtobufLibrary();
    }
};
static ProtobufRAII s_protobuf;

namespace protorpc {
typedef std::shared_ptr<protorpc::Request>  RequestPtr;
typedef std::shared_ptr<protorpc::Response> ResponsePtr;

enum ProtoRpcResult {
    kRpcSuccess     = 0,
    kRpcTimeout     = -1,
    kRpcError       = -2,
    kRpcNoResult    = -3,
    kRpcParseError  = -4,
};

class ProtoRpcContext {
public:
    protorpc::RequestPtr                req;
    std::promise<protorpc::ResponsePtr> res;
};
typedef std::shared_ptr<ProtoRpcContext>    ContextPtr;

class ProtoRpcClient : public TcpClient {
public:
    ProtoRpcClient() : TcpClient()
    {
        connect_status = kInitialized;

        // reconnect setting
        reconn_setting_t reconn;
        reconn_setting_init(&reconn);
        reconn.min_delay = 1000;
        reconn.max_delay = 10000;
        reconn.delay_policy = 2;
        setReconnect(&reconn);

        // init protorpc_unpack_setting
        unpack_setting_t protorpc_unpack_setting;
        memset(&protorpc_unpack_setting, 0, sizeof(unpack_setting_t));
        protorpc_unpack_setting.mode = UNPACK_BY_LENGTH_FIELD;
        protorpc_unpack_setting.package_max_length = DEFAULT_PACKAGE_MAX_LENGTH;
        protorpc_unpack_setting.body_offset = PROTORPC_HEAD_LENGTH;
        protorpc_unpack_setting.length_field_offset = PROTORPC_HEAD_LENGTH_FIELD_OFFSET;
        protorpc_unpack_setting.length_field_bytes = PROTORPC_HEAD_LENGTH_FIELD_BYTES;
        protorpc_unpack_setting.length_field_coding = ENCODE_BY_BIG_ENDIAN;
        setUnpack(&protorpc_unpack_setting);

        onConnection = [this](const SocketChannelPtr& channel) {
            std::string peeraddr = channel->peeraddr();
            if (channel->isConnected()) {
                connect_status = kConnected;
                printf("connected to %s! connfd=%d\n", peeraddr.c_str(), channel->fd());
            } else {
                connect_status = kDisconnectd;
                printf("disconnected to %s! connfd=%d\n", peeraddr.c_str(), channel->fd());
            }
        };

        onMessage = [this](const SocketChannelPtr& channel, Buffer* buf) {
            // protorpc_unpack
            protorpc_message msg;
            memset(&msg, 0, sizeof(msg));
            int packlen = protorpc_unpack(&msg, buf->data(), buf->size());
            if (packlen < 0) {
                printf("protorpc_unpack failed!\n");
                return;
            }
            assert(packlen == buf->size());
            if (protorpc_head_check(&msg.head) != 0) {
                printf("protorpc_head_check failed!\n");
                return;
            }
            // Response::ParseFromArray
            auto res = std::make_shared<protorpc::Response>();
            if (!res->ParseFromArray(msg.body, msg.head.length)) {
                return;
            }
            // id => res
            calls_mutex.lock();
            auto iter = calls.find(res->id());
            if (iter == calls.end()) {
                calls_mutex.unlock();
                return;
            }
            auto ctx = iter->second;
            calls_mutex.unlock();
            ctx->res.set_value(res);
        };
    }

    // @retval >0 connfd, <0 error, =0 connecting
    int connect(int port, const char* host = "127.0.0.1", bool wait_connect = true, int connect_timeout = 5000) {
        int fd = createsocket(port, host);
        if (fd < 0) {
            return fd;
        }
        setConnectTimeout(connect_timeout);
        connect_status = kConnecting;
        start();
        if (wait_connect) {
            while (connect_status == kConnecting) hv_msleep(1);
            return connect_status == kConnected ? fd : -1;
        }
        return 0;
    }

    bool isConnected() {
        return connect_status == kConnected;
    }

    protorpc::ResponsePtr call(protorpc::RequestPtr& req, int timeout_ms = 10000) {
        if (!isConnected()) {
            printf("RPC not connected!\n");
            return NULL;
        }
        static std::atomic<uint64_t> s_id = ATOMIC_VAR_INIT(0);
        req->set_id(++s_id);
        req->id();
        auto ctx = std::make_shared<protorpc::ProtoRpcContext>();
        ctx->req = req;
        calls_mutex.lock();
        calls[req->id()] = ctx;
        calls_mutex.unlock();
        // Request::SerializeToArray + protorpc_pack
        protorpc_message msg;
        protorpc_message_init(&msg);
        msg.head.length = req->ByteSize();
        int packlen = protorpc_package_length(&msg.head);
        unsigned char* writebuf = NULL;
        HV_STACK_ALLOC(writebuf, packlen);
        packlen = protorpc_pack(&msg, writebuf, packlen);
        if (packlen > 0) {
            printf("%s\n", req->DebugString().c_str());
            req->SerializeToArray(writebuf + PROTORPC_HEAD_LENGTH, msg.head.length);
            channel->write(writebuf, packlen);
        }
        HV_STACK_FREE(writebuf);
        protorpc::ResponsePtr res;
        if (timeout_ms > 0) {
            // wait until response come or timeout
            auto fut = ctx->res.get_future();
            auto status = fut.wait_for(std::chrono::milliseconds(timeout_ms));
            if (status == std::future_status::ready) {
                res = fut.get();
                if (res->has_error()) {
                    printf("RPC error:\n%s\n", res->error().DebugString().c_str());
                }
            } else if (status == std::future_status::timeout) {
                printf("RPC timeout!\n");
            } else {
                printf("RPC unexpected status: %d!\n", (int)status);
            }
        }
        calls_mutex.lock();
        calls.erase(req->id());
        calls_mutex.unlock();
        return res;
    }

    int calc(const char* method, int num1, int num2, int& out) {
        auto req = std::make_shared<protorpc::Request>();
        // method
        req->set_method(method);
        // params
        protorpc::CalcParam param1, param2;
        param1.set_num(num1);
        param2.set_num(num2);
        req->add_params()->assign(param1.SerializeAsString());
        req->add_params()->assign(param2.SerializeAsString());

        auto res = call(req);

        if (res == NULL) return kRpcTimeout;
        if (res->has_error()) return kRpcError;
        if (res->result().empty()) return kRpcNoResult;
        protorpc::CalcResult result;
        if (!result.ParseFromString(res->result())) return kRpcParseError;
        out = result.num();
        return kRpcSuccess;
    }

    int login(const protorpc::LoginParam& param, protorpc::LoginResult* result) {
        auto req = std::make_shared<protorpc::Request>();
        // method
        req->set_method("login");
        // params
        req->add_params()->assign(param.SerializeAsString());

        auto res = call(req);

        if (res == NULL) return kRpcTimeout;
        if (res->has_error()) return kRpcError;
        if (res->result().empty()) return kRpcNoResult;
        if (!result->ParseFromString(res->result())) return kRpcParseError;
        return kRpcSuccess;
    }

private:
    enum ConnectStatus {
        kInitialized,
        kConnecting,
        kConnected,
        kDisconnectd,
    };
    std::atomic<ConnectStatus> connect_status;
    std::map<uint64_t, protorpc::ContextPtr> calls;
    std::mutex calls_mutex;
};
}

int main(int argc, char** argv) {
    if (argc < 6) {
        printf("Usage: %s host port method param1 param2\n", argv[0]);
        printf("method = [add, sub, mul, div]\n");
        printf("Examples:\n");
        printf("  %s 127.0.0.1 1234 add 1 2\n", argv[0]);
        printf("  %s 127.0.0.1 1234 div 1 0\n", argv[0]);
        return -10;
    }
    const char* host = argv[1];
    int port = atoi(argv[2]);
    const char* method = argv[3];
    const char* param1 = argv[4];
    const char* param2 = argv[5];

    protorpc::ProtoRpcClient cli;
    int ret = cli.connect(port, host, true);
    if (ret < 0) {
        return -20;
    }

    // test login
    {
        protorpc::LoginParam param;
        param.set_username("admin");
        param.set_password("123456");
        protorpc::LoginResult result;
        if (cli.login(param, &result) == protorpc::kRpcSuccess) {
            printf("login success!\n");
            printf("%s\n", result.DebugString().c_str());
        } else {
            printf("login failed!\n");
        }
    }

    // test calc
    {
        int num1 = atoi(param1);
        int num2 = atoi(param2);
        int result = 0;
        if (cli.calc(method, num1, num2, result) == protorpc::kRpcSuccess) {
            printf("calc success!\n");
            printf("%d %s %d = %d\n", num1, method, num2, result);
        } else {
            printf("calc failed!\n");
        }
    }
    return 0;
}
