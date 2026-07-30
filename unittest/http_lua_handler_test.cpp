#include "HttpScriptHandler.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "hbase.h"
#include "hfile.h"
#include "hpath.h"
#include "HttpService.h"
#include "HttpContext.h"
#include "EventLoop.h"

static std::string write_script(const char* name, const char* content) {
    std::string dir = "tmp/http_lua_handler_test";
    const char* slash = strrchr(name, '/');
    if (slash) {
        dir = HPath::join(dir, std::string(name, slash - name));
    }
    hv_mkdir_p(dir.c_str());
    std::string path = HPath::join("tmp/http_lua_handler_test", name);
    HFile file;
    int ret = file.open(path.c_str(), "wb");
    assert(ret == 0);
    file.write(content, strlen(content));
    file.close();
    return path;
}

static HttpContextPtr make_ctx(const char* method, const char* path) {
    HttpContextPtr ctx = std::make_shared<hv::HttpContext>();
    ctx->request = std::make_shared<HttpRequest>();
    ctx->response = std::make_shared<HttpResponse>();
    ctx->request->method = http_method_enum(method);
    ctx->request->path = path;
    ctx->request->query_params["id"] = "42";
    ctx->request->headers["X-Test"] = "header-value";
    ctx->request->body = "request-body";
    return ctx;
}

static void test_text_response() {
    std::string script = write_script("text.lua",
        "function handle(ctx)\n"
        "  ctx:status(201)\n"
        "  ctx:set_header('X-Lua', ctx:query('id'))\n"
        "  return ctx:text(ctx:method() .. ' ' .. ctx:path() .. ' ' .. ctx:header('X-Test'))\n"
        "end\n");

    hv::HttpScriptHandler handler(script.c_str());
    HttpContextPtr ctx = make_ctx("GET", "/hello");
    int status = handler(ctx);
    assert(status == 201);
    assert(ctx->response->status_code == 201);
    assert(ctx->response->body == "GET /hello header-value");
    assert(ctx->response->GetHeader("X-Lua") == "42");
    assert(ctx->response->ContentType() == TEXT_PLAIN);
}

static void test_json_response() {
    std::string script = write_script("json.lua",
        "function handle(ctx)\n"
        "  return ctx:json({ok=true, id=ctx:query('id')})\n"
        "end\n");

    hv::HttpScriptHandler handler(script.c_str());
    HttpContextPtr ctx = make_ctx("POST", "/json");
    int status = handler(ctx);
    assert(status == 200);
    assert(ctx->response->ContentType() == APPLICATION_JSON);
    assert(ctx->response->body.find("\"ok\": true") != std::string::npos);
    assert(ctx->response->body.find("\"id\": \"42\"") != std::string::npos);
}

static void test_method_function_preferred() {
    std::string script = write_script("method.lua",
        "function get(ctx)\n"
        "  return ctx:text('get:' .. ctx:query('id'))\n"
        "end\n"
        "function handle(ctx)\n"
        "  return ctx:text('handle')\n"
        "end\n");

    hv::HttpScriptHandler handler(script.c_str());
    HttpContextPtr ctx = make_ctx("GET", "/method");
    int status = handler(ctx);
    assert(status == 200);
    assert(ctx->response->body == "get:42");
}

static void test_method_function_fallback_to_handle() {
    std::string script = write_script("method_fallback.lua",
        "function get(ctx)\n"
        "  return ctx:text('get')\n"
        "end\n"
        "function handle(ctx)\n"
        "  return ctx:text('fallback:' .. ctx:method())\n"
        "end\n");

    hv::HttpScriptHandler handler(script.c_str());
    HttpContextPtr ctx = make_ctx("POST", "/method");
    int status = handler(ctx);
    assert(status == 200);
    assert(ctx->response->body == "fallback:POST");
}

static void test_script_dir_mapping() {
    write_script("api/user.lua",
        "function handle(ctx)\n"
        "  return ctx:text('script:' .. ctx:query('id'))\n"
        "end\n");

    hv::HttpService service;
    service.Script("/api/", "tmp/http_lua_handler_test/api");

    http_handler* handler = NULL;
    std::map<std::string, std::string> params;
    int ret = service.GetRoute("/api/user?id=42", HTTP_GET, &handler, params);
    assert(ret == 0);
    assert(handler != NULL);
    assert(handler->ctx_handler != NULL);

    HttpContextPtr ctx = make_ctx("GET", "/api/user");
    ctx->service = &service;
    ctx->request->query_params = params;
    ctx->request->query_params["id"] = "42";
    int status = handler->ctx_handler(ctx);
    assert(status == 200);
    assert(ctx->response->body == "script:42");
}

static void test_script_dir_parent_path_forbidden() {
    hv::HttpService service;
    service.Script("/api/", "tmp/http_lua_handler_test/api");

    http_handler* handler = NULL;
    std::map<std::string, std::string> params;
    int ret = service.GetRoute("/api/foo/..bar", HTTP_GET, &handler, params);
    assert(ret == 0);
    assert(handler != NULL);
    assert(handler->ctx_handler != NULL);

    HttpContextPtr ctx = make_ctx("GET", "/api/foo/..bar");
    ctx->service = &service;
    int status = handler->ctx_handler(ctx);
    assert(status == HTTP_STATUS_FORBIDDEN);
}

static void test_unknown_script_suffix() {
    std::string script = write_script("unknown.py", "def handle(ctx): pass\n");

    hv::HttpScriptHandler handler(script.c_str());
    HttpContextPtr ctx = make_ctx("GET", "/unknown");
    int status = handler(ctx);
    assert(status == HTTP_STATUS_NOT_IMPLEMENTED);
    assert(ctx->response->status_code == HTTP_STATUS_NOT_IMPLEMENTED);
}

int main() {
    // HttpLuaHandler runs on the IO thread's per-loop lua_State, obtained via
    // currentThreadEventLoop. Bind an EventLoop to this thread's TLS so the
    // handler can create/reuse its lua_State (these scripts finish synchronously).
    // Use make_shared (not a stack object) so that if a script ever reaches a
    // client binding (hv.http/redis/ws/mqtt -> currentThreadEventLoopPtr ->
    // shared_from_this()), it resolves to a real EventLoopPtr instead of the
    // bad_weak_ptr fallback — matching the other lua tests and avoiding a future
    // "silently no shared loop" trap.
    hv::EventLoopPtr loop = std::make_shared<hv::EventLoop>();
    hv::ThreadLocalStorage::set(hv::ThreadLocalStorage::EVENT_LOOP, loop.get());

    test_text_response();
    test_json_response();
    test_method_function_preferred();
    test_method_function_fallback_to_handle();
    test_script_dir_mapping();
    test_script_dir_parent_path_forbidden();
    test_unknown_script_suffix();
    printf("ALL http_lua_handler_test PASSED\n");
    return 0;
}
