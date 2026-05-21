#include <assert.h>
#include <stdio.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "HttpRouter.h"

int main() {
    hv::HttpRouter<int> router;
    std::map<std::string, std::string> params;
    int value = 0;

    router.Insert("/hello", 1);
    router.Insert("/users/:id", 2);
    router.Insert("/orders/{orderId}", 3);
    router.Insert("/wildcard*", 4);
    router.Insert("/www.*.html", 5);
    router.Insert("*", 404);
    router.Insert("/users/list", 6);
    router.Insert("/dup", 7);
    router.Insert("/dup", 8);

    assert(router.Match("/hello", value, params));
    assert(value == 1);
    assert(params.empty());
    printf("Match /hello\n");

    params.clear();
    assert(router.Match("/users/123", value, params));
    assert(value == 2);
    assert(params["id"] == "123");
    printf("Match /users/:id\n");

    params.clear();
    assert(router.Match("/orders/42", value, params));
    assert(value == 3);
    assert(params["orderId"] == "42");
    printf("Match /orders/{orderId}\n");

    params.clear();
    assert(router.Match("/wildcard-tail", value, params));
    assert(value == 4);
    assert(params.empty());
    printf("Match /wildcard*\n");

    params.clear();
    assert(router.Match("/www.index.html", value, params));
    assert(value == 5);
    assert(params.empty());
    printf("Match /www.*.html\n");

    params.clear();
    assert(router.Match("/users/list", value, params));
    assert(value == 6);
    assert(params.empty());
    printf("Match /users/list\n");

    params.clear();
    assert(router.Match("/dup", value, params));
    assert(value == 8);
    assert(params.empty());
    printf("Match /dup\n");

    params["keep"] = "yes";
    assert(router.Match("/missing", value, params));
    assert(value == 404);
    assert(params["keep"] == "yes");
    printf("Match *\n");

    std::vector<std::string> paths = router.Paths();
    int dup_count = 0;
    bool has_colon_param_route = false;
    bool has_brace_param_route = false;
    bool has_wildcard_route = false;
    bool has_suffix_wildcard_route = false;
    bool has_any_wildcard_route = false;
    for (const auto& path : paths) {
        if (path == "/dup") {
            ++dup_count;
        }
        if (path == "/users/:id") {
            has_colon_param_route = true;
        }
        if (path == "/orders/{orderId}") {
            has_brace_param_route = true;
        }
        if (path == "/wildcard*") {
            has_wildcard_route = true;
        }
        if (path == "/www.*.html") {
            has_suffix_wildcard_route = true;
        }
        if (path == "*") {
            has_any_wildcard_route = true;
        }
    }
    assert(dup_count == 1);
    assert(has_colon_param_route);
    assert(has_brace_param_route);
    assert(has_wildcard_route);
    assert(has_suffix_wildcard_route);
    assert(has_any_wildcard_route);

    hv::HttpRouter<std::shared_ptr<int>> literal_router;
    std::shared_ptr<int> literal_value;
    std::map<std::string, std::string> literal_params;

    literal_router.Insert("/users/:id", std::make_shared<int>(1));
    assert(!literal_router.Find("/users/list", literal_value));

    literal_value = std::make_shared<int>(2);
    literal_router.Insert("/users/list", literal_value);

    std::shared_ptr<int> matched_value;
    literal_params.clear();
    assert(literal_router.Match("/users/list", matched_value, literal_params));
    assert(matched_value);
    assert(*matched_value == 2);
    assert(literal_params.empty());
    printf("Match /users/list\n");

    literal_params.clear();
    assert(literal_router.Match("/users/123", matched_value, literal_params));
    assert(matched_value);
    assert(*matched_value == 1);
    assert(literal_params["id"] == "123");
    printf("Match /users/:id\n");

    return 0;
}
