#ifndef HV_HTTP_ROUTER_H_
#define HV_HTTP_ROUTER_H_

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace hv {

template<typename Handler>
struct RouteNode {
    std::unordered_map<std::string, std::unique_ptr<RouteNode<Handler>>> children;
    std::unique_ptr<RouteNode<Handler>> param_child;
    std::string param_name;
    Handler handler;
    bool has_handler;

    RouteNode() : has_handler(false) {}
};

template<typename Handler>
struct WildcardRoute {
    std::string pattern;
    std::string prefix;
    std::string suffix;
    Handler     handler;

    bool Match(const std::string& path) const {
        if (!prefix.empty() && path.compare(0, prefix.size(), prefix) != 0) {
            return false;
        }
        if (suffix.empty()) {
            return true;
        }
        return path.size() >= prefix.size() + suffix.size() &&
               path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0;
    }
};

namespace detail {

inline std::vector<std::string> splitPathSegments(const std::string& path) {
    std::vector<std::string> segments;
    const char* p = path.c_str();
    const char* segment = NULL;
    while (*p != '\0') {
        if (*p == '/') {
            if (segment != NULL) {
                segments.push_back(std::string(segment, p - segment));
                segment = NULL;
            }
        }
        else if (segment == NULL) {
            segment = p;
        }
        ++p;
    }
    if (segment != NULL) {
        segments.push_back(std::string(segment, p - segment));
    }
    return segments;
}

inline bool isParamSegment(const std::string& segment) {
    // RESTful style 1 /user/:id
    // RESTful style 2 /user/{id}
    return (!segment.empty() && segment[0] == ':') ||
           (segment.size() >= 3 && segment.front() == '{' && segment.back() == '}');
}

inline std::string paramNameOf(const std::string& segment) {
    if (!segment.empty() && segment[0] == ':') {
        return segment.substr(1);
    }
    if (segment.size() >= 3 && segment.front() == '{' && segment.back() == '}') {
        return segment.substr(1, segment.size() - 2);
    }
    return std::string();
}

template<typename Handler>
inline bool matchNode(const RouteNode<Handler>* node, const std::vector<std::string>& segments, size_t index, Handler& handler, std::map<std::string, std::string>& params) {
    if (index == segments.size()) {
        if (!node->has_handler) {
            return false;
        }
        handler = node->handler;
        return true;
    }

    auto literal_iter = node->children.find(segments[index]);
    if (literal_iter != node->children.end() && matchNode(literal_iter->second.get(), segments, index + 1, handler, params)) {
        return true;
    }

    if (node->param_child && matchNode(node->param_child.get(), segments, index + 1, handler, params)) {
        params[node->param_name] = segments[index];
        return true;
    }

    return false;
}

} // namespace detail

template<typename Handler>
class HttpRouter {
public:
    HttpRouter() : has_param_routes_(false) {}

    void Clear() {
        routes_.clear();
        param_root_ = RouteNode<Handler>();
        has_param_routes_ = false;
        wildcard_routes_.clear();
    }

    void Insert(const std::string& path, const Handler& handler) {
        // all routes
        routes_[path] = handler;

        // wildcard routes
        size_t wildcard_pos = path.find('*');
        if (wildcard_pos != std::string::npos) {
            for (auto& route : wildcard_routes_) {
                if (route.pattern == path) {
                    route.handler = handler;
                    return;
                }
            }
            WildcardRoute<Handler> route;
            route.pattern = path;
            route.prefix = path.substr(0, wildcard_pos);
            route.suffix = path.substr(wildcard_pos + 1);
            route.handler = handler;
            wildcard_routes_.push_back(route);
            return;
        }

        // param routes
        if (path.find("/:") != std::string::npos || path.find("/{") != std::string::npos) {
            std::vector<std::string> segments = detail::splitPathSegments(path);
            RouteNode<Handler>* node = &param_root_;
            for (const auto& segment : segments) {
                if (detail::isParamSegment(segment)) {
                    if (!node->param_child) {
                        node->param_child.reset(new RouteNode<Handler>());
                    }
                    node->param_name = detail::paramNameOf(segment);
                    node = node->param_child.get();
                    continue;
                }

                std::unique_ptr<RouteNode<Handler>>& child = node->children[segment];
                if (!child) {
                    child.reset(new RouteNode<Handler>());
                }
                node = child.get();
            }
            node->handler = handler;
            node->has_handler = true;
            has_param_routes_ = true;
        }
    }

    bool Find(const std::string& path, Handler& handler) const {
        auto route_iter = routes_.find(path);
        if (route_iter == routes_.end()) {
            return false;
        }
        handler = route_iter->second;
        return true;
    }

    bool MatchParam(const std::string& path, Handler& handler, std::map<std::string, std::string>& params) const {
        if (!has_param_routes_) {
            return false;
        }
        std::vector<std::string> segments = detail::splitPathSegments(path);
        return detail::matchNode(&param_root_, segments, 0, handler, params);
    }

    bool MatchWildcard(const std::string& path, Handler& handler) const {
        for (const auto& wildcard_route : wildcard_routes_) {
            if (wildcard_route.Match(path)) {
                handler = wildcard_route.handler;
                return true;
            }
        }
        return false;
    }

    bool Match(const std::string& path, Handler& handler, std::map<std::string, std::string>& params) const {
        // Literal > Param > Wildcard
        return Find(path, handler) ||
               MatchParam(path, handler, params) ||
               MatchWildcard(path, handler);
    }

    bool Empty() const {
        return routes_.empty();
    }

    std::vector<std::string> Paths() const {
        std::vector<std::string> paths;
        paths.reserve(routes_.size());
        for (const auto& route : routes_) {
            paths.push_back(route.first);
        }
        return paths;
    }

private:
    std::unordered_map<std::string, Handler> routes_;
    RouteNode<Handler> param_root_;
    bool has_param_routes_;
    std::vector<WildcardRoute<Handler>> wildcard_routes_;
};

}

#endif // HV_HTTP_ROUTER_H_
