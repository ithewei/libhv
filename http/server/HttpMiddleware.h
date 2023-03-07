#ifndef HV_HTTP_MIDDLEWARE_H_
#define HV_HTTP_MIDDLEWARE_H_

#include "hexport.h"
#include "HttpContext.h"

BEGIN_NAMESPACE_HV

class HttpMiddleware {
public:
    static int CORS(HttpRequest* req, HttpResponse* resp);
};

END_NAMESPACE_HV

#endif // HV_HTTP_MIDDLEWARE_H_
