/*
 * hrpc calc server
 *
 * @build   see examples/rpc/README (needs protobuf + generated calc.pb / calc.hrpc)
 * @server  bin/hrpc_calc_server 1234
 * @client  bin/hrpc_calc_client 127.0.0.1 1234
 */

#include <stdio.h>

#include "calc.hrpc.h"
#include "htime.h"

using namespace hv;

class CalcServiceImpl : public calc::CalcService {
public:
    rpc::RpcStatus Add(const calc::AddRequest& req, calc::AddReply* resp) override {
        resp->set_result(req.a() + req.b());
        return rpc::RpcStatus();
    }
    rpc::RpcStatus Sub(const calc::AddRequest& req, calc::AddReply* resp) override {
        resp->set_result(req.a() - req.b());
        return rpc::RpcStatus();
    }
};

int main(int argc, char** argv) {
    int port = argc > 1 ? atoi(argv[1]) : 1234;

    rpc::RpcServer srv;
    if (srv.createsocket(port) < 0) {
        printf("listen port %d failed\n", port);
        return -1;
    }
    srv.registerService(std::make_shared<CalcServiceImpl>());
    srv.setThreadNum(4);
    srv.start();
    printf("rpc_calc_server listening on %d ...\n", port);

    while (1) hv_sleep(1);
    return 0;
}
