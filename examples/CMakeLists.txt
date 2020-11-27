list(APPEND EXAMPLES
    hmain_test
    hloop_test
    htimer_test
    tcp
    udp
    nc
    nmap
)

include_directories(.. ../base ../event ../utils)

add_executable(hmain_test hmain_test.cpp)
target_link_libraries(hmain_test hv)

add_executable(hloop_test hloop_test.c)
target_link_libraries(hloop_test hv)

add_executable(htimer_test htimer_test.c)
target_link_libraries(htimer_test hv)

add_executable(tcp tcp.c)
target_link_libraries(tcp hv)

add_executable(udp udp.c)
target_link_libraries(udp hv)

add_executable(nc nc.c)
target_link_libraries(nc hv)

add_executable(nmap nmap.cpp)
target_compile_definitions(nmap PRIVATE -DPRINT_DEBUG)
target_link_libraries(nmap hv)

if(WITH_HTTP)
    include_directories(../http)
if(WITH_HTTP_SERVER)
    include_directories(../http/server)
    list(APPEND EXAMPLES httpd)
    aux_source_directory(httpd HTTPD_SRCS)
    add_executable(httpd ${HTTPD_SRCS})
    target_link_libraries(httpd hv)
endif()

if(WITH_HTTP_CLIENT)
    include_directories(../http/client)
    set(CURL_TARGET_NAME curl)
    if(WITH_CURL)
        set(CURL_TARGET_NAME hv_curl)
    endif()
    list(APPEND EXAMPLES ${CURL_TARGET_NAME})
    add_executable(${CURL_TARGET_NAME} curl.cpp)
    if(WITH_CURL)
        set_target_properties(${CURL_TARGET_NAME} PROPERTIES OUTPUT_NAME curl)
    endif()
    target_link_libraries(${CURL_TARGET_NAME} hv)
endif()

if(WITH_CONSUL)
    include_directories(../consul)
    list(APPEND EXAMPLES consul_cli)
    add_executable(consul_cli consul_cli.cpp)
    target_compile_definitions(consul_cli PRIVATE PRINT_DEBUG)
    target_link_libraries(consul_cli hv)
endif()
endif()

add_custom_target(examples DEPENDS ${EXAMPLES})
