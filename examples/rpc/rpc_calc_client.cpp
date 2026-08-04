/*
 * hrpc calc client
 *
 * @server  bin/hrpc_calc_server 1234
 * @client  bin/hrpc_calc_client 127.0.0.1 1234
 */

#include <stdio.h>

#include "calc.hrpc.h"
#include "htime.h"

using namespace hv;

int main(int argc, char** argv) {
    const char* host = argc > 1 ? argv[1] : "127.0.0.1";
    int port = argc > 2 ? atoi(argv[2]) : 1234;

    rpc::RpcClient client;
    client.createsocket(port, host);
    client.start();

    // wait for connect (sync calls must not run on the loop thread; here main
    // thread is a separate caller thread, so blocking is fine)
    for (int i = 0; i < 300 && !client.isConnected(); ++i) hv_msleep(10);
    if (!client.isConnected()) {
        printf("connect %s:%d failed\n", host, port);
        return -1;
    }

    calc::CalcStub stub(&client);

    // sync call
    {
        calc::AddRequest req;
        req.set_a(7);
        req.set_b(5);
        calc::AddReply reply;
        rpc::RpcStatus st = stub.Add(req, &reply);
        if (st.ok()) printf("[sync]  Add(7, 5) = %lld\n", (long long)reply.result());
        else printf("[sync]  Add failed: %d %s\n", st.code, st.message.c_str());
    }

    // async call
    {
        calc::AddRequest req;
        req.set_a(7);
        req.set_b(5);
        bool done = false;
        stub.Sub(req, [&done](const rpc::RpcStatus& st, const calc::AddReply& reply) {
            if (st.ok()) printf("[async] Sub(7, 5) = %lld\n", (long long)reply.result());
            else printf("[async] Sub failed: %d %s\n", st.code, st.message.c_str());
            done = true;
        });
        for (int i = 0; i < 300 && !done; ++i) hv_msleep(10);
    }

    return 0;
}
