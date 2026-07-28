#include "HttpServer.h"
#include "HttpScriptHandler.h"

using namespace hv;

int main(int argc, char** argv) {
    int port = 8080;
    const char* script_dir = "examples/scripts";
    if (argc > 1) {
        port = atoi(argv[1]);
    }
    if (argc > 2) {
        script_dir = argv[2];
    }

    HttpService router;
    router.GET("/hello", HttpScriptHandler("examples/scripts/hello.lua"));
    router.Script("/script/", script_dir);

    HttpServer server;
    server.port = port;
    server.service = &router;
    server.setThreadNum(4);
    server.run();
    return 0;
}
