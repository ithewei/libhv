#include "hplatform.h"

#ifdef OS_WIN
#ifdef ENABLE_WINDUMP
#include <dbghelp.h>
#ifdef _MSC_VER
#pragma comment(lib,"dbghelp.lib")
#endif
static LONG UnhandledException(EXCEPTION_POINTERS *pException) {
    char modulefile[256];
    GetModuleFileName(NULL, modulefile, sizeof(modulefile));
    char* pos = strrchr(modulefile, '\\');
    char* modulefilename = pos + 1;
    SYSTEMTIME st;
    GetLocalTime(&st);
    char filename[256];
    snprintf(filename, sizeof(filename), "core_%s_%04d%02d%02d_%02d%02d%02d_%03d.dump",
        modulefilename,
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    HANDLE hDumpFile = CreateFile(filename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    MINIDUMP_EXCEPTION_INFORMATION dumpInfo;
    dumpInfo.ExceptionPointers = pException;
    dumpInfo.ThreadId = GetCurrentThreadId();
    dumpInfo.ClientPointers = TRUE;
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hDumpFile, MiniDumpNormal, &dumpInfo, NULL, NULL);
    CloseHandle(hDumpFile);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

#include "hsocket.h"
class WsaRAII {
public:
    WsaRAII() {
        WSAInit();
#ifdef ENABLE_WINDUMP
        SetUnhandledExceptionFilter((LPTOP_LEVEL_EXCEPTION_FILTER)UnhandledException);
#endif
    }
    ~WsaRAII() {
        WSADeinit();
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
