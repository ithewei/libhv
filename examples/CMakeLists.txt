list(APPEND EXAMPLES
    hloop_test
    htimer_test
    nc
    tcp_echo_server
    tcp_chat_server
    tcp_proxy_server
    udp_echo_server
    udp_proxy_server
    jsonrpc
)

include_directories(.. ../base ../ssl ../event ../util)

add_executable(hloop_test hloop_test.c)
target_link_libraries(hloop_test ${HV_LIBRARIES})

add_executable(htimer_test htimer_test.c)
target_link_libraries(htimer_test ${HV_LIBRARIES})

add_executable(nc nc.c)
target_link_libraries(nc ${HV_LIBRARIES})

add_executable(tcp_echo_server tcp_echo_server.c)
target_link_libraries(tcp_echo_server ${HV_LIBRARIES})

add_executable(tcp_chat_server tcp_chat_server.c)
target_link_libraries(tcp_chat_server ${HV_LIBRARIES})

add_executable(tcp_proxy_server tcp_proxy_server.c)
target_link_libraries(tcp_proxy_server ${HV_LIBRARIES})

add_executable(udp_echo_server udp_echo_server.c)
target_link_libraries(udp_echo_server ${HV_LIBRARIES})

add_executable(udp_proxy_server udp_proxy_server.c)
target_link_libraries(udp_proxy_server ${HV_LIBRARIES})

add_executable(jsonrpc_client jsonrpc/jsonrpc_client.c jsonrpc/jsonrpc.c jsonrpc/cJSON.c)
target_link_libraries(jsonrpc_client ${HV_LIBRARIES})

add_executable(jsonrpc_server jsonrpc/jsonrpc_server.c jsonrpc/jsonrpc.c jsonrpc/cJSON.c)
target_link_libraries(jsonrpc_server ${HV_LIBRARIES})

if(WITH_EVPP)
    include_directories(../cpputil ../evpp)
    add_executable(hmain_test hmain_test.cpp)
    target_link_libraries(hmain_test ${HV_LIBRARIES})

    aux_source_directory(nmap NMAP_SRCS)
    add_executable(nmap ${NMAP_SRCS})
    target_compile_definitions(nmap PRIVATE PRINT_DEBUG)
    target_link_libraries(nmap ${HV_LIBRARIES})

    list(APPEND EXAMPLES hmain_test nmap)
if(WITH_HTTP)
    include_directories(../http)
if(WITH_HTTP_SERVER)
    include_directories(../http/server)

    # http_server_test
    add_executable(http_server_test http_server_test.cpp)
    target_link_libraries(http_server_test ${HV_LIBRARIES})

    # websocket_server_test
    add_executable(websocket_server_test websocket_server_test.cpp)
    target_link_libraries(websocket_server_test ${HV_LIBRARIES})

    list(APPEND EXAMPLES http_server_test websocket_server_test)
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
    target_link_libraries(${CURL_TARGET_NAME} ${HV_LIBRARIES})

    # wget
    add_executable(wget wget.cpp)
    target_link_libraries(wget ${HV_LIBRARIES})

    # consul
    aux_source_directory(consul CONSUL_SRCS)
    add_executable(consul ${CONSUL_SRCS})
    target_compile_definitions(consul PRIVATE PRINT_DEBUG)
    target_link_libraries(consul ${HV_LIBRARIES})

    # http_client_test
    add_executable(http_client_test http_client_test.cpp)
    target_link_libraries(http_client_test ${HV_LIBRARIES})

    # websocket_client_test
    add_executable(websocket_client_test websocket_client_test.cpp)
    target_link_libraries(websocket_client_test ${HV_LIBRARIES})

    list(APPEND EXAMPLES ${CURL_TARGET_NAME} wget consul http_client_test websocket_client_test)

    if(WITH_HTTP_SERVER)
        # httpd
        aux_source_directory(httpd HTTPD_SRCS)
        add_executable(httpd ${HTTPD_SRCS})
        target_link_libraries(httpd ${HV_LIBRARIES})
        list(APPEND EXAMPLES httpd)
    endif()
endif()

endif()
endif()

add_custom_target(jsonrpc DEPENDS jsonrpc_client jsonrpc_server)
add_custom_target(examples DEPENDS ${EXAMPLES})
