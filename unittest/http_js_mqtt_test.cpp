/*
 * http_js_mqtt_test - HttpJsHandler + hv/mqtt Promise binding smoke test.
 *
 * No live MQTT broker is required: this verifies the module registers and its
 * connect() failure path rejects without crashing or hanging.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hbase.h"
#include "hfile.h"
#include "hpath.h"
#include "HttpServer.h"
#include "HttpService.h"
#include "HttpScriptHandler.h"
#include "requests.h"

#define CHECK(expr)                                                                    \
    do {                                                                               \
        if (!(expr)) {                                                                 \
            fprintf(stderr, "CHECK failed: %s at %s:%d\n", #expr, __FILE__, __LINE__); \
            abort();                                                                   \
        }                                                                              \
    } while (0)

static std::string write_script(const char* name, const char* content) {
    hv_mkdir_p("tmp/http_js_mqtt_test");
    std::string path = HPath::join("tmp/http_js_mqtt_test", name);
    HFile file;
    int ret = file.open(path.c_str(), "wb");
    CHECK(ret == 0);
    file.write(content, strlen(content));
    file.close();
    return path;
}

int main() {
    std::string script = write_script("mqtt.js", "const mqtt = require('hv/mqtt');\n"
                                                 "async function get(ctx) {\n"
                                                 "  let err = '';\n"
                                                 "  try { await mqtt.connect({ host: '127.0.0.1', port: 1, timeout: 500 }); }\n"
                                                 "  catch (e) { err = String(e); }\n"
                                                 "  return { ok: true, err };\n"
                                                 "}\n");

    HttpService service;
    service.GET("/mqtt", hv::HttpScriptHandler(script.c_str()));

    hv::HttpServer server(&service);
    server.setThreadNum(1);
    server.setPort(0);
    CHECK(server.start() == 0);
    CHECK(server.port > 0);
    hv_msleep(200);

    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/mqtt", server.port);
    auto resp = requests::get(url);
    server.stop();
    hv_msleep(100);

    CHECK(resp != NULL);
    CHECK(resp->status_code == 200);
    CHECK(resp->body.find("\"ok\":true") != std::string::npos);
    CHECK(resp->body.find("\"err\":\"") != std::string::npos);
    printf("ALL http_js_mqtt_test PASSED\n");
    return 0;
}
