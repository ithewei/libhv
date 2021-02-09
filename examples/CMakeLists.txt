list(APPEND EXAMPLES
    hmain_test
    hloop_test
    htimer_test
    nc
    nmap
    udp_echo_server
    tcp_echo_server
    tcp_chat_server
    tcp_proxy_server
)

include_directories(.. ../base ../utils ../event ../evpp)

add_executable(hmain_test hmain_test.cpp)
target_link_libraries(hmain_test hv)

add_executable(hloop_test hloop_test.c)
target_link_libraries(hloop_test hv)

add_executable(htimer_test htimer_test.c)
target_link_libraries(htimer_test hv)

add_executable(nc nc.c)
target_link_libraries(nc hv)

add_executable(nmap nmap.cpp)
target_compile_definitions(nmap PRIVATE -DPRINT_DEBUG)
target_link_libraries(nmap hv)

add_executable(udp_echo_server udp_echo_server.c)
target_link_libraries(udp_echo_server hv)

add_executable(tcp_echo_server tcp_echo_server.c)
target_link_libraries(tcp_echo_server hv)

add_executable(tcp_chat_server tcp_chat_server.c)
target_link_libraries(tcp_chat_server hv)

add_executable(tcp_proxy_server tcp_proxy_server.c)
target_link_libraries(tcp_proxy_server hv)

if(WITH_HTTP)
    include_directories(../http)
if(WITH_HTTP_SERVER)
    include_directories(../http/server)

    # httpd
    aux_source_directory(httpd HTTPD_SRCS)
    add_executable(httpd ${HTTPD_SRCS})
    target_link_libraries(httpd hv)

    # http_server_test
    add_executable(http_server_test http_server_test.cpp)
    target_link_libraries(http_server_test hv)

    # websocket_server_test
    add_executable(websocket_server_test websocket_server_test.cpp)
    target_link_libraries(websocket_server_test hv)

    list(APPEND EXAMPLES httpd http_server_test websocket_server_test)
endif()

if(WITH_HTTP_CLIENT)
    include_directories(../http/client)

    # curl
    set(CURL_TARGET_NAME curl)
    if(WITH_CURL)
        set(CURL_TARGET_NAME hv_curl)
    endif()
    add_executable(${CURL_TARGET_NAME} curl.cpp)
    if(WITH_CURL)
        set_target_properties(${CURL_TARGET_NAME} PROPERTIES OUTPUT_NAME curl)
    endif()
    target_link_libraries(${CURL_TARGET_NAME} hv)

    # http_client_test
    add_executable(http_client_test http_client_test.cpp)
    target_link_libraries(http_client_test hv)

    # websocket_client_test
    add_executable(websocket_client_test websocket_client_test.cpp)
    target_link_libraries(websocket_client_test hv)

    list(APPEND EXAMPLES ${CURL_TARGET_NAME} http_client_test websocket_client_test)
endif()

if(WITH_CONSUL)
    include_directories(../consul)

    add_executable(consul_cli consul_cli.cpp)
    target_compile_definitions(consul_cli PRIVATE PRINT_DEBUG)
    target_link_libraries(consul_cli hv)

    list(APPEND EXAMPLES consul_cli)
endif()
endif()

add_custom_target(examples DEPENDS ${EXAMPLES})
