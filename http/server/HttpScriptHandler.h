#ifndef HV_HTTP_SCRIPT_HANDLER_H_
#define HV_HTTP_SCRIPT_HANDLER_H_

#include <memory>
#include <string>

#include "hexport.h"
#include "HttpContext.h"

namespace hv {

struct HV_EXPORT HttpScriptHandlerOptions {
    bool reload_on_change;

    HttpScriptHandlerOptions() {
        reload_on_change = true;
    }
};

class HV_EXPORT HttpScriptHandler {
public:
    HttpScriptHandler(const char* filepath, const HttpScriptHandlerOptions& options = HttpScriptHandlerOptions());
    HttpScriptHandler(const HttpScriptHandler& rhs);
    HttpScriptHandler& operator=(const HttpScriptHandler& rhs);

    int operator()(const HttpContextPtr& ctx);

    const std::string& filepath() const {
        return filepath_;
    }

private:
    struct State;

    std::string       filepath_;
    HttpScriptHandlerOptions options_;
    std::shared_ptr<State> state_;
};

typedef std::shared_ptr<HttpScriptHandler> HttpScriptHandlerPtr;

} // namespace hv

#endif // HV_HTTP_SCRIPT_HANDLER_H_
