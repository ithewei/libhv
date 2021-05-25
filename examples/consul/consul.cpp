#include "consul.h"

#include "http_client.h"

#include "json.hpp"
using json = nlohmann::json;

#define PROTOCOL    "http://"
#define API_VERSION "v1"

static const char url_register[] = "/agent/service/register";
static const char url_deregister[] = "/agent/service/deregister";
static const char url_discover[] = "/catalog/service";

static string make_url(const char* ip, int port, const char* url) {
    return asprintf(PROTOCOL "%s:%d/" API_VERSION "%s", ip, port, url);
}

static string make_ServiceID(consul_service_t* service) {
    return asprintf("%s-%s:%d", service->name, service->ip, service->port);
}

/*
{
  "ID": "redis1",
  "Name": "redis",
  "Tags": [
    "primary",
    "v1"
  ],
  "Address": "127.0.0.1",
  "Port": 8000,
  "Meta": {
    "redis_version": "4.0"
  },
  "EnableTagOverride": false,
  "Check": {
    "DeregisterCriticalServiceAfter": "90m",
    "Args": ["/usr/local/bin/check_redis.py"],
    "HTTP": "http://localhost:5000/health",
    "Interval": "10s",
    "TTL": "15s"
  },
  "Weights": {
    "Passing": 10,
    "Warning": 1
  }
}
 */
int register_service(consul_node_t* node, consul_service_t* service, consul_health_t* health) {
    HttpRequest req;
    req.method = HTTP_PUT;
    req.url = make_url(node->ip, node->port, url_register);
    req.content_type = APPLICATION_JSON;

    json jservice;
    jservice["Name"] = service->name;
    if (*service->ip) {
        jservice["Address"] = service->ip;
    }
    jservice["Port"] = service->port;
    jservice["ID"] = make_ServiceID(service);

    json jcheck;
    if (*health->url == '\0') {
        snprintf(health->url, sizeof(health->url), "%s:%d", service->ip, service->port);
    }
    jcheck[health->protocol] = health->url;
    jcheck["Interval"] = asprintf("%dms", health->interval);
    jcheck["DeregisterCriticalServiceAfter"] = asprintf("%dms", health->interval * 3);
    jservice["Check"] = jcheck;

    req.body = jservice.dump();
    printd("PUT %s\n", req.url.c_str());
    printd("%s\n", req.body.c_str());

    HttpResponse res;
    int ret = http_client_send(&req, &res);
    printd("%s\n", res.body.c_str());
    return ret;
}

int deregister_service(consul_node_t* node, consul_service_t* service) {
    string url = make_url(node->ip, node->port, url_deregister);
    url += '/';
    url += make_ServiceID(service);

    HttpRequest req;
    req.method = HTTP_PUT;
    req.url = url;
    req.content_type = APPLICATION_JSON;
    printd("PUT %s\n", req.url.c_str());

    HttpResponse res;
    int ret = http_client_send(&req, &res);
    printd("%s\n", res.body.c_str());
    return ret;
}

int discover_services(consul_node_t* node, const char* service_name, std::vector<consul_service_t>& services) {
    string url = make_url(node->ip, node->port, url_discover);
    url += '/';
    url += service_name;

    HttpRequest req;
    req.method = HTTP_GET;
    req.url = url;

    HttpResponse res;
    printd("GET %s\n", req.url.c_str());
    int ret = http_client_send(&req, &res);
    if (ret != 0) return ret;
    printd("%s\n", res.body.c_str());

    json jroot = json::parse(res.body);
    if (!jroot.is_array()) return -1;
    if (jroot.size() == 0) return 0;

    consul_service_t service;
    std::string name, ip;
    services.clear();
    for (size_t i = 0; i < jroot.size(); ++i) {
        auto jservice = jroot[i];
        name = jservice["ServiceName"];
        if (jservice.contains("Address")) {
            ip = jservice["Address"];
        } else if (jservice.contains("ServiceAddress")) {
            ip = jservice["ServiceAddress"];
        } else if (jservice.contains("ServiceAddress6")) {
            ip = jservice["ServiceAddress6"];
        } else {
            continue;
        }
        int port = jservice["ServicePort"];

        strncpy(service.name, name.c_str(), sizeof(service.name));
        strncpy(service.ip, ip.c_str(), sizeof(service.ip));
        service.port = port;
        services.emplace_back(service);
    }

    return 0;
}
