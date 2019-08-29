#include "hplatform.h"

#ifdef OS_WIN
class WsaRAII {
public:
    WsaRAII() {
        WSADATA wsadata;
        WSAStartup(MAKEWORD(2,2), &wsadata);
    }
    ~WsaRAII() {
        WSACleanup();
    }
};
static WsaRAII s_wsa;
#endif

#ifdef WITH_CURL
#include "curl/curl.h"
#ifdef _MSC_VER
//#pragma comment(lib, "libcurl.a")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "wldap32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "crypt32.lib")
#endif
class CurlRAII {
public:
    CurlRAII() {
        curl_global_init(CURL_GLOBAL_ALL);
    }
    ~CurlRAII() {
        curl_global_cleanup();
    }
};
static CurlRAII s_curl;
#endif

#ifdef WITH_OPENSSL
#include "openssl/ssl.h"
#include "openssl/err.h"
#ifdef _MSC_VER
//#pragma comment(lib, "libssl.a")
//#pragma comment(lib, "libcrypto.a")
#endif
class OpensslRAII {
public:
    OpensslRAII() {
        OPENSSL_init_ssl(OPENSSL_INIT_SSL_DEFAULT, NULL);
    }

    ~OpensslRAII() {
    }
};
static OpensslRAII s_openssl;
#endif
