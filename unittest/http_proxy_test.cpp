#include <assert.h>

#include "HttpMessage.h"
#include "AsyncHttpClient.h"

int main() {
    HttpRequest req;
    req.SetUrl("http://origin.example:8081/a");
    req.ParseUrl();
    req.SetProxy("proxy.example", 3128);
    assert(req.IsProxy());
    assert(req.IsUriProxy());
    assert(!req.IsTunnelProxy());
    assert(req.host == "origin.example");
    assert(req.port == 8081);
    assert(req.proxy_host == "proxy.example");
    assert(req.proxy_port == 3128);
    hv::HttpConnKey uri_key(req);
    assert(uri_key.target_host == "origin.example");
    assert(uri_key.target_port == 8081);
    assert(uri_key.proxy_host == "proxy.example");
    assert(uri_key.proxy_port == 3128);
    assert(!uri_key.tls);

    req.SetProxyAuth("user", "pass");
    assert(req.proxy_username == "user");
    assert(req.proxy_password == "pass");
    assert(req.headers.find("Proxy-Authorization") == req.headers.end());
    std::string proxy_request = req.Dump();
    assert(proxy_request.find("GET http://origin.example:8081/a HTTP/1.1\r\n") == 0);
    assert(proxy_request.find("Proxy-Authorization: Basic dXNlcjpwYXNz\r\n") != std::string::npos);
    assert(req.headers.find("Proxy-Authorization") == req.headers.end());
    req.SetProxyAuth(NULL);
    assert(req.proxy_username.empty());
    assert(req.proxy_password.empty());

    req.Reset();
    req.SetUrl("https://origin.example/secure");
    req.ParseUrl();
    req.SetProxy("proxy.example", 3128);
    assert(req.IsProxy());
    assert(!req.IsUriProxy());
    assert(req.IsTunnelProxy());
    hv::HttpConnKey tunnel_key(req);
    assert(tunnel_key.target_host == "origin.example");
    assert(tunnel_key.proxy_host == "proxy.example");
    assert(tunnel_key.tls);
    assert(uri_key != tunnel_key);

    req.SetUrl("http://redirect.example:8082/next");
    req.ParseUrl();
    assert(req.host == "redirect.example");
    assert(req.port == 8082);
    assert(req.IsUriProxy());
    return 0;
}
