#ifndef GRPC_SERVER_H
#define GRPC_SERVER_H

#include "grpcpp/grpcpp.h"
using grpc::ServerBuilder;
using grpc::Server;
using grpc::Service;
using grpc::ServerCompletionQueue;

class GrpcServer {
public:
    GrpcServer(int port) : _port(port) {
        char srvaddr[128] = {0};
        snprintf(srvaddr, sizeof(srvaddr), "%s:%d", "0.0.0.0", _port);
        build_.AddListeningPort(srvaddr, grpc::InsecureServerCredentials());
        build_.AddChannelArgument(GRPC_ARG_KEEPALIVE_TIME_MS,    30000);
        build_.AddChannelArgument(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, 5000);
        build_.AddChannelArgument(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, 1);
    }

    void RegisterService(Service* service) {
        build_.RegisterService(service);
    }

    void Run(bool wait=true) {
        server_ = build_.BuildAndStart();
        if (wait) {
            server_->Wait();
        }
    }

public:
    int _port;
    ServerBuilder build_;
    std::unique_ptr<Server> server_;
};

#endif // GRPC_SERVER_H
