cmake_minimum_required(VERSION 3.6)
cmake_policy(SET CMP0077 NEW)

project(hv VERSION 1.3.4)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(LIBHV_FIND_DEPENDENCY_OPENSSL FALSE)

option(BUILD_SHARED "build shared library" ON)
option(BUILD_STATIC "build static library" ON)

option(BUILD_EXAMPLES "build examples" ON)
option(BUILD_UNITTEST "build unittest" OFF)

# see config.ini
option(WITH_PROTOCOL "compile protocol" OFF)

option(WITH_EVPP "compile evpp" ON)
option(WITH_HTTP "compile http" ON)
option(WITH_HTTP_SERVER "compile http/server" ON)
option(WITH_HTTP_CLIENT "compile http/client" ON)
option(WITH_MQTT "compile mqtt" OFF)
option(WITH_REDIS "compile redis" OFF)
option(WITH_RPC "compile hrpc (libhrpc, needs protobuf)" OFF)

option(ENABLE_UDS "Unix Domain Socket" OFF)
option(USE_MULTIMAP "MultiMap" OFF)

option(WITH_CURL "with curl library (deprecated)" OFF)
option(WITH_NGHTTP2 "with nghttp2 library" OFF)

option(WITH_OPENSSL "with openssl library" OFF)
option(WITH_GNUTLS  "with gnutls library"  OFF)
option(WITH_MBEDTLS "with mbedtls library" OFF)

option(WITH_LUA "with lua library" OFF)
option(WITH_JS "with quickjs library" OFF)

option(WITH_KCP "compile event/kcp" OFF)

if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    option(WITH_IO_URING "with io_uring" OFF)
endif()

if(WIN32 OR MINGW)
    option(WITH_WEPOLL "compile event/wepoll -> use iocp" ON)
    option(ENABLE_WINDUMP "Windows MiniDumpWriteDump" OFF)
    option(BUILD_FOR_MT "build for /MT" OFF)
    if(BUILD_FOR_MT)
        set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /MTd")
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
        set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MT")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
    endif()
endif()

message(STATUS "CMAKE_SOURCE_DIR=${CMAKE_SOURCE_DIR}")
message(STATUS "CMAKE_CURRENT_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}")
if(NOT "${CMAKE_CURRENT_SOURCE_DIR}" STREQUAL "${CMAKE_SOURCE_DIR}")
    set(BUILD_EXAMPLES OFF)
endif()

if(IOS)
    set(BUILD_SHARED OFF)
    set(BUILD_EXAMPLES OFF)
endif()

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake;${CMAKE_MODULE_PATH}")
include(utils)
include(vars)

# see configure
# Checks for header files
check_header("stdbool.h")
check_header("stdint.h")
check_header("stdatomic.h")
check_header("sys/types.h")
check_header("sys/stat.h")
check_header("sys/time.h")
check_header("fcntl.h")
check_header("pthread.h")
check_header("endian.h")
check_header("sys/endian.h")

# Checks for functions
if(NOT MSVC)
    set(CMAKE_REQUIRED_LIBRARIES "-pthread")
endif()
check_function("gettid" "unistd.h")
check_function("strlcpy" "string.h")
check_function("strlcat" "string.h")
check_function("clock_gettime" "time.h")
check_function("gettimeofday" "sys/time.h")
check_function("pthread_spin_lock" "pthread.h")
check_function("pthread_mutex_timedlock" "pthread.h")
check_function("sem_timedwait" "semaphore.h")
check_function("pipe" "unistd.h")
check_function("socketpair" "sys/socket.h")
check_function("eventfd" "sys/eventfd.h")
check_function("setproctitle" "unistd.h")

if (NOT HAVE_CLOCK_GETTIME)
    include(CheckLibraryExists)
    check_library_exists(rt clock_gettime "" HAVE_CLOCK_GETTIME_IN_RT)
    if (HAVE_CLOCK_GETTIME_IN_RT)
        set(HAVE_CLOCK_GETTIME ${HAVE_CLOCK_GETTIME_IN_RT})
    endif()
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/hconfig.h.in ${CMAKE_CURRENT_SOURCE_DIR}/hconfig.h)

# see Makefile.in
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED True)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_C_EXTENSIONS ON)
endif()
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(INCDIR include)
set(SRCDIR src)
set(LIBDIR lib)
set(BINDIR bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LIBDIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${LIBDIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${BINDIR})
message(STATUS "CMAKE_LIBRARY_OUTPUT_DIRECTORY=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")

set(INCDIRS . include 3rd/include)
set(LIBDIRS . lib 3rd/lib)
include_directories(${INCDIRS} ${SRCDIR})
link_directories(${LIBDIRS})

message(STATUS "CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
if("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
    add_definitions(-DDEBUG)
else()
    add_definitions(-DNDEBUG)
endif()

if(ENABLE_UDS)
    add_definitions(-DENABLE_UDS)
endif()

if(USE_MULTIMAP)
    add_definitions(-DUSE_MULTIMAP)
endif()

if(WITH_CURL)
    add_definitions(-DWITH_CURL)
    set(LIBS ${LIBS} curl)
    if(WIN32 OR MINGW)
        set(LIBS ${LIBS} wldap32 advapi32 crypt32)
    endif()
endif()

if(WITH_NGHTTP2)
    add_definitions(-DWITH_NGHTTP2)
    set(LIBS ${LIBS} nghttp2)
endif()

if(WITH_OPENSSL)
    add_definitions(-DWITH_OPENSSL)
    find_package(OpenSSL)
    if(OpenSSL_FOUND)
        set(LIBS ${LIBS} OpenSSL::SSL OpenSSL::Crypto)
        set(LIBHV_FIND_DEPENDENCY_OPENSSL ON)
    else()
        set(LIBS ${LIBS} ssl crypto)
    endif()
endif()

if(WITH_GNUTLS)
    add_definitions(-DWITH_GNUTLS)
    set(LIBS ${LIBS} gnutls)
endif()

if(WITH_MBEDTLS)
    add_definitions(-DWITH_MBEDTLS)
    set(LIBS ${LIBS} mbedtls mbedx509 mbedcrypto)
endif()

if(WITH_IO_URING)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "WITH_IO_URING is only supported on Linux because liburing is Linux-only.")
    endif()
    set(LIBS ${LIBS} uring)
endif()

if(WITH_LUA)
    add_definitions(-DWITH_LUA)
    find_package(Lua)
    if(LUA_FOUND)
        include_directories(${LUA_INCLUDE_DIR})
        set(LIBS ${LIBS} ${LUA_LIBRARIES})
    else()
        set(LIBS ${LIBS} lua)
    endif()
    # lua bindings that wrap higher-level modules are conditionally compiled on
    # HVLUA_WITH_* (kept in sync with Makefile.in): enabled only when both WITH_LUA
    # and the underlying module are on. Without these, the http/redis/ws/mqtt
    # binding bodies are #ifdef'd out and hv.http/redis/ws/mqtt silently become nil.
    if(WITH_EVPP AND WITH_HTTP AND WITH_HTTP_CLIENT)
        add_definitions(-DHVLUA_WITH_HTTP)
    endif()
    if(WITH_EVPP AND WITH_REDIS)
        add_definitions(-DHVLUA_WITH_REDIS)
    endif()
    if(WITH_EVPP AND WITH_MQTT)
        add_definitions(-DHVLUA_WITH_MQTT)
    endif()
endif()

if(WITH_JS)
    add_definitions(-DWITH_JS)
    find_path(QUICKJS_INCLUDE_DIR
        NAMES quickjs.h
        HINTS
            ${QUICKJS_ROOT}/include/quickjs
            ${QUICKJS_ROOT}/include
            /opt/homebrew/opt/quickjs/include/quickjs
            /opt/homebrew/opt/quickjs/include
            /usr/local/opt/quickjs/include/quickjs
            /usr/local/opt/quickjs/include
            /usr/local/include/quickjs
            /usr/local/include
            /usr/include/quickjs
            /usr/include)
    find_library(QUICKJS_LIBRARY
        NAMES quickjs libquickjs
        HINTS
            ${QUICKJS_ROOT}/lib/quickjs
            ${QUICKJS_ROOT}/lib
            /opt/homebrew/opt/quickjs/lib/quickjs
            /opt/homebrew/opt/quickjs/lib
            /usr/local/opt/quickjs/lib/quickjs
            /usr/local/opt/quickjs/lib
            /usr/local/lib/quickjs
            /usr/local/lib
            /usr/lib)
    if(NOT QUICKJS_INCLUDE_DIR OR NOT QUICKJS_LIBRARY)
        message(FATAL_ERROR "WITH_JS requires QuickJS. Set QUICKJS_ROOT or QUICKJS_INCLUDE_DIR and QUICKJS_LIBRARY.")
    endif()
    include_directories(${QUICKJS_INCLUDE_DIR})
    set(LIBS ${LIBS} ${QUICKJS_LIBRARY})
    if(WITH_EVPP AND WITH_HTTP AND WITH_HTTP_CLIENT)
        add_definitions(-DHVJS_WITH_HTTP)
    endif()
    if(WITH_EVPP AND WITH_REDIS)
        add_definitions(-DHVJS_WITH_REDIS)
    endif()
    if(WITH_EVPP AND WITH_MQTT)
        add_definitions(-DHVJS_WITH_MQTT)
    endif()
endif()

if(WIN32 OR MINGW)
    add_definitions(-DWIN32_LEAN_AND_MEAN -D_CRT_SECURE_NO_WARNINGS -D_WIN32_WINNT=0x0600)
    set(LIBS ${LIBS} secur32 crypt32 winmm iphlpapi ws2_32)
    if(ENABLE_WINDUMP)
        add_definitions(-DENABLE_WINDUMP)
        set(LIBS ${LIBS} dbghelp)
    endif()
endif()

if(ANDROID)
    set(LIBS ${LIBS} log)
elseif(UNIX AND NOT MINGW)
    set(LIBS ${LIBS} pthread m dl)
    find_library(RT_LIBRARY rt)
    if(RT_LIBRARY)
        set(LIBS ${LIBS} rt)
    endif()
    # Some platforms (e.g. 32-bit ARM / Raspberry Pi) need libatomic for
    # 64-bit atomics; link it only when available. See issue #384.
    find_library(ATOMIC_LIBRARY atomic)
    if(ATOMIC_LIBRARY)
        set(LIBS ${LIBS} atomic)
    endif()
endif()

if(APPLE)
    set(LIBS ${LIBS} "-framework CoreFoundation" "-framework Security")
endif()

# see Makefile
set(ALL_SRCDIRS . base ssl event event/kcp util cpputil evpp redis protocol http http/client http/server mqtt)
set(CORE_SRCDIRS . base ssl event)
if(WIN32 OR MINGW)
    if(WITH_WEPOLL)
        set(CORE_SRCDIRS ${CORE_SRCDIRS} event/wepoll)
    endif()
endif()
if(WITH_KCP)
    set(CORE_SRCDIRS ${CORE_SRCDIRS} event/kcp)
endif()
set(LIBHV_SRCDIRS ${CORE_SRCDIRS} util)
set(LIBHV_HEADERS hv.h hconfig.h hexport.h)
set(LIBHV_HEADERS ${LIBHV_HEADERS} ${BASE_HEADERS} ${SSL_HEADERS} ${EVENT_HEADERS} ${UTIL_HEADERS})

if(WITH_PROTOCOL)
    set(LIBHV_HEADERS ${LIBHV_HEADERS} ${PROTOCOL_HEADERS})
    set(LIBHV_SRCDIRS ${LIBHV_SRCDIRS} protocol)
endif()

if(WITH_LUA)
    set(LIBHV_HEADERS ${LIBHV_HEADERS} lua/hvlua.h lua/hvlua_json.h lua/hvlua_util.h)
    set(LIBHV_SRCDIRS ${LIBHV_SRCDIRS} lua)
    if(NOT WITH_EVPP)
        set(LIBHV_HEADERS ${LIBHV_HEADERS} ${CPPUTIL_HEADERS})
        set(LIBHV_SRCDIRS ${LIBHV_SRCDIRS} cpputil)
    endif()
endif()

if(WITH_EVPP)
    set(LIBHV_HEADERS ${LIBHV_HEADERS} ${CPPUTIL_HEADERS} ${EVPP_HEADERS})
    set(LIBHV_SRCDIRS ${LIBHV_SRCDIRS} cpputil evpp)
    if(WITH_REDIS)
        set(LIBHV_HEADERS ${LIBHV_HEADERS} ${REDIS_HEADERS})
        set(LIBHV_SRCDIRS ${LIBHV_SRCDIRS} redis)
    endif()
    if(WITH_HTTP)
        set(LIBHV_HEADERS ${LIBHV_HEADERS} ${HTTP_HEADERS})
        set(LIBHV_SRCDIRS ${LIBHV_SRCDIRS} http)
        if(WITH_NGHTTP2)
            set(LIBHV_HEADERS ${LIBHV_HEADERS} ${HTTP2_HEADERS})
        endif()
        if(WITH_HTTP_SERVER)
            set(LIBHV_HEADERS ${LIBHV_HEADERS} ${HTTP_SERVER_HEADERS})
            if(WITH_LUA OR WITH_JS)
                set(LIBHV_HEADERS ${LIBHV_HEADERS} http/server/HttpScriptHandler.h)
            endif()
            if(WITH_LUA)
                set(LIBHV_HEADERS ${LIBHV_HEADERS} http/server/HttpLuaHandler.h)
            endif()
            if(WITH_JS)
                set(LIBHV_HEADERS ${LIBHV_HEADERS} http/server/HttpJsHandler.h)
            endif()
            set(LIBHV_SRCDIRS ${LIBHV_SRCDIRS} http/server)
        endif()
        if(WITH_HTTP_CLIENT)
            set(LIBHV_HEADERS ${LIBHV_HEADERS} ${HTTP_CLIENT_HEADERS})
            set(LIBHV_SRCDIRS ${LIBHV_SRCDIRS} http/client)
        endif()
    endif()

    if(CMAKE_SYSTEM_NAME MATCHES "Linux" AND CMAKE_COMPILER_IS_GNUCC)
        set(LIBS ${LIBS} stdc++)
    endif()
endif()

if(WITH_MQTT)
    set(LIBHV_HEADERS ${LIBHV_HEADERS} ${MQTT_HEADERS})
    set(LIBHV_SRCDIRS ${LIBHV_SRCDIRS} mqtt)
endif()

list_source_directories(LIBHV_SRCS ${LIBHV_SRCDIRS})
if(NOT WITH_LUA)
    list(FILTER LIBHV_SRCS EXCLUDE REGEX "(^|/)HttpLuaHandler\\.cpp$")
endif()
if(NOT WITH_JS)
    list(FILTER LIBHV_SRCS EXCLUDE REGEX "(^|/)HttpJsHandler\\.cpp$")
endif()
if(NOT WITH_LUA AND NOT WITH_JS)
    list(FILTER LIBHV_SRCS EXCLUDE REGEX "(^|/)HttpScriptHandler\\.cpp$")
endif()
if(WIN32)
    set(CMAKE_RC_FLAGS_DEBUG -D_DEBUG)
    configure_file(${PROJECT_SOURCE_DIR}/${PROJECT_NAME}.rc.in ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.rc)
    list(APPEND LIBHV_SRCS ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.rc)
endif()

file(INSTALL ${LIBHV_HEADERS} DESTINATION include/hv)
file(INSTALL ${LIBHV_HEADERS} DESTINATION ${PROJECT_SOURCE_DIR}/include/hv)

if(BUILD_SHARED)
    add_library(hv SHARED ${LIBHV_SRCS})
    target_compile_definitions(hv PRIVATE HV_DYNAMICLIB)
    target_include_directories(hv PRIVATE ${LIBHV_SRCDIRS}
        INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_link_libraries(hv PUBLIC ${LIBS})
    install(TARGETS hv
        EXPORT libhvTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    add_custom_target(libhv DEPENDS hv)
endif()

if(BUILD_STATIC)
    add_library(hv_static STATIC ${LIBHV_SRCS})
    target_compile_definitions(hv_static PUBLIC HV_STATICLIB)
    target_include_directories(hv_static PRIVATE ${LIBHV_SRCDIRS}
        INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_link_libraries(hv_static PUBLIC ${LIBS})
    if(NOT (WIN32 AND BUILD_SHARED))
        set_target_properties(hv_static PROPERTIES OUTPUT_NAME hv)
    endif()
    install(TARGETS hv_static
        EXPORT libhvTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    add_custom_target(libhv_static DEPENDS hv_static)
endif()

if(WIN32 AND NOT MINGW)
    if(BUILD_SHARED)
        install(FILES $<TARGET_PDB_FILE:${PROJECT_NAME}> DESTINATION ${CMAKE_INSTALL_BINDIR} OPTIONAL)
    endif()
endif()

install(FILES ${LIBHV_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/hv)

set(LIBHV_INSTALL_CMAKEDIR ${CMAKE_INSTALL_LIBDIR}/cmake/libhv)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/libhvConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/libhvConfig.cmake
    INSTALL_DESTINATION ${LIBHV_INSTALL_CMAKEDIR}
    PATH_VARS CMAKE_INSTALL_INCLUDEDIR CMAKE_INSTALL_LIBDIR)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/libhvConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/libhvConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/libhvConfigVersion.cmake
    DESTINATION ${LIBHV_INSTALL_CMAKEDIR})
install(EXPORT libhvTargets
    NAMESPACE libhv::
    FILE libhvTargets.cmake
    DESTINATION ${LIBHV_INSTALL_CMAKEDIR})

# hrpc: TLV + protobuf RPC, built as a separate library (libhrpc) so libhv
# stays protobuf-free. See rpc/CMakeLists.txt. Must precede examples (they link hrpc).
if(WITH_RPC)
    add_subdirectory(rpc)
endif()

if(BUILD_SHARED)
    set(HV_LIBRARIES hv CACHE INTERNAL "link hv libraries")
else()
    add_definitions(-DHV_STATICLIB)
    set(HV_LIBRARIES hv_static ${LIBS} CACHE INTERNAL "link hv libraries")
endif()

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
    # for httpd -c etc/httpd.conf
    file(INSTALL etc DESTINATION ${CMAKE_BINARY_DIR})
    file(INSTALL etc DESTINATION ${CMAKE_BINARY_DIR}/bin)
    file(INSTALL etc DESTINATION ${CMAKE_BINARY_DIR}/examples)
endif()

if(BUILD_UNITTEST)
    add_subdirectory(unittest)
endif()

# CPack settings
set(CPACK_PACKAGE_NAME "libhv")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_RELEASE 1)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A high-performance C/C++ network library")
set(CPACK_PACKAGE_VENDOR "libhv")
set(CPACK_PACKAGE_CONTACT "ithewei <ithewei@163.com>")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_HOST_SYSTEM_PROCESSOR}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

# Specify the package generators
set(CPACK_GENERATOR "TGZ;DEB;RPM")

# Enable CPack debug output
set(CPACK_PACKAGE_DEBUG True)

# https://cmake.org/cmake/help/latest/variable/CPACK_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION.html
set(CPACK_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION "ON")
include(CPack)
