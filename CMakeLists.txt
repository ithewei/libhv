cmake_minimum_required(VERSION 3.6)

project(hv VERSION 1.3.0)

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

option(ENABLE_UDS "Unix Domain Socket" OFF)
option(USE_MULTIMAP "MultiMap" OFF)

option(WITH_CURL "with curl library" OFF)
option(WITH_NGHTTP2 "with nghttp2 library" OFF)

option(WITH_OPENSSL "with openssl library" OFF)
option(WITH_GNUTLS  "with gnutls library"  OFF)
option(WITH_MBEDTLS "with mbedtls library" OFF)

option(WITH_KCP "with kcp" OFF)

if(WIN32)
    option(ENABLE_WINDUMP "Windows MiniDumpWriteDump" OFF)
    option(BUILD_FOR_MT "build for /MT" OFF)
    if(BUILD_FOR_MT)
        set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /MTd")
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
        set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MT")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
    endif()
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

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/hconfig.h.in ${CMAKE_CURRENT_SOURCE_DIR}/hconfig.h)

# see Makefile.in
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED True)
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
    if(WIN32)
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

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -D_CRT_SECURE_NO_WARNINGS -D_WIN32_WINNT=0x0600)
    set(LIBS ${LIBS} winmm iphlpapi ws2_32)
    if(ENABLE_WINDUMP)
        add_definitions(-DENABLE_WINDUMP)
        set(LIBS ${LIBS} dbghelp)
    endif()
endif()

if(ANDROID)
    set(LIBS ${LIBS} log)
elseif(UNIX)
    set(LIBS ${LIBS} pthread m dl)
    if(CMAKE_COMPILER_IS_GNUCC)
        set(LIBS ${LIBS} rt)
    endif()
endif()

if(APPLE)
    set(LIBS ${LIBS} "-framework CoreFoundation" "-framework Security")
endif()

# see Makefile
set(ALL_SRCDIRS . base ssl event event/kcp util cpputil evpp protocol http http/client http/server mqtt)
set(CORE_SRCDIRS . base ssl event)
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

if(WITH_EVPP)
    set(LIBHV_HEADERS ${LIBHV_HEADERS} ${CPPUTIL_HEADERS} ${EVPP_HEADERS})
    set(LIBHV_SRCDIRS ${LIBHV_SRCDIRS} cpputil evpp)
    if(WITH_HTTP)
        set(LIBHV_HEADERS ${LIBHV_HEADERS} ${HTTP_HEADERS})
        set(LIBHV_SRCDIRS ${LIBHV_SRCDIRS} http)
        if(WITH_NGHTTP2)
            set(LIBHV_HEADERS ${LIBHV_HEADERS} ${HTTP2_HEADERS})
        endif()
        if(WITH_HTTP_SERVER)
            set(LIBHV_HEADERS ${LIBHV_HEADERS} ${HTTP_SERVER_HEADERS})
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

file(INSTALL ${LIBHV_HEADERS} DESTINATION include/hv)
file(INSTALL ${LIBHV_HEADERS} DESTINATION ${PROJECT_SOURCE_DIR}/include/hv)

if(BUILD_SHARED)
    add_library(hv SHARED ${LIBHV_SRCS})
    target_compile_definitions(hv PRIVATE HV_DYNAMICLIB)
    target_include_directories(hv PRIVATE ${LIBHV_SRCDIRS}
        INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
    target_link_libraries(hv ${LIBS})
    install(TARGETS hv
        EXPORT libhvConfig
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin)
    add_custom_target(libhv DEPENDS hv)
endif()

if(BUILD_STATIC)
    add_library(hv_static STATIC ${LIBHV_SRCS})
    target_compile_definitions(hv_static PUBLIC HV_STATICLIB)
    target_include_directories(hv_static PRIVATE ${LIBHV_SRCDIRS}
        INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
    target_link_libraries(hv_static ${LIBS})
    install(TARGETS hv_static
        EXPORT libhvConfig
        ARCHIVE DESTINATION lib)
    add_custom_target(libhv_static DEPENDS hv_static)
endif()

install(FILES ${LIBHV_HEADERS} DESTINATION include/hv)
install(EXPORT libhvConfig DESTINATION lib/cmake/libhv)

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
