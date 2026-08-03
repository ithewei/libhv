list(APPEND EXAMPLES
    hloop_test
    htimer_test
    pipe_test
    nc
    tinyhttpd
    tinyproxyd
    tcp_client_test
    tcp_echo_server
    tcp_chat_server
    tcp_proxy_server
    udp_echo_server
    udp_proxy_server
    socks5_proxy_server
    host
    multi-acceptor-processes
    multi-acceptor-threads
    one-acceptor-multi-workers
    jsonrpc_client
    jsonrpc_server
)

include_directories(.. ../base ../ssl ../event ../util)

add_executable(hloop_test hloop_test.c)
target_link_libraries(hloop_test ${HV_LIBRARIES})

add_executable(htimer_test htimer_test.c)
target_link_libraries(htimer_test ${HV_LIBRARIES})

add_executable(pipe_test pipe_test.c)
target_link_libraries(pipe_test ${HV_LIBRARIES})

add_executable(nc nc.c)
target_link_libraries(nc ${HV_LIBRARIES})

add_executable(tinyhttpd tinyhttpd.c)
target_link_libraries(tinyhttpd ${HV_LIBRARIES})

add_executable(tinyproxyd tinyproxyd.c)
target_link_libraries(tinyproxyd ${HV_LIBRARIES})

add_executable(tcp_client_test tcp_client_test.c)
target_link_libraries(tcp_client_test ${HV_LIBRARIES})

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

add_executable(socks5_proxy_server socks5_proxy_server.c)
target_link_libraries(socks5_proxy_server ${HV_LIBRARIES})

add_executable(host host.c)
target_link_libraries(host ${HV_LIBRARIES})

add_executable(multi-acceptor-processes multi-thread/multi-acceptor-processes.c)
target_link_libraries(multi-acceptor-processes ${HV_LIBRARIES})

add_executable(multi-acceptor-threads multi-thread/multi-acceptor-threads.c)
target_link_libraries(multi-acceptor-threads ${HV_LIBRARIES})

add_executable(one-acceptor-multi-workers multi-thread/one-acceptor-multi-workers.c)
target_link_libraries(one-acceptor-multi-workers ${HV_LIBRARIES})

add_executable(jsonrpc_client jsonrpc/jsonrpc_client.c jsonrpc/cJSON.c)
target_compile_definitions(jsonrpc_client PRIVATE CJSON_HIDE_SYMBOLS)
target_link_libraries(jsonrpc_client ${HV_LIBRARIES})

add_executable(jsonrpc_server jsonrpc/jsonrpc_server.c jsonrpc/cJSON.c)
target_compile_definitions(jsonrpc_server PRIVATE CJSON_HIDE_SYMBOLS)
target_link_libraries(jsonrpc_server ${HV_LIBRARIES})

if(WITH_KCP)
    glob_headers_and_sources(KCPTUN_SMUX_FILES   kcptun/smux)
    glob_headers_and_sources(KCPTUN_CLIENT_FILES kcptun/client)
    glob_headers_and_sources(KCPTUN_SERVER_FILES kcptun/server)

    # kcptun_client
    add_executable(kcptun_client ${KCPTUN_SMUX_FILES} ${KCPTUN_CLIENT_FILES})
    target_link_libraries(kcptun_client ${HV_LIBRARIES})

    # kcptun_server
    add_executable(kcptun_server ${KCPTUN_SMUX_FILES} ${KCPTUN_SERVER_FILES})
    target_link_libraries(kcptun_server ${HV_LIBRARIES})

    list(APPEND EXAMPLES kcptun_client kcptun_server)
endif()

if(WITH_EVPP)
    include_directories(../cpputil ../evpp)

    # hmain_test
    add_executable(hmain_test hmain_test.cpp)
    target_link_libraries(hmain_test ${HV_LIBRARIES})

    # nmap
    glob_headers_and_sources(NMAP_FILES nmap)
    add_executable(nmap ${NMAP_FILES})
    target_compile_definitions(nmap PRIVATE PRINT_DEBUG)
    target_link_libraries(nmap ${HV_LIBRARIES})

    list(APPEND EXAMPLES hmain_test nmap)

    if(WITH_REDIS)
        include_directories(../redis)

        add_executable(redis_client_example redis_client_test.cpp)
        target_link_libraries(redis_client_example ${HV_LIBRARIES})

        add_executable(redis_subscriber_example redis_subscriber_test.cpp)
        target_link_libraries(redis_subscriber_example ${HV_LIBRARIES})

        list(APPEND EXAMPLES redis_client_example redis_subscriber_example)
    endif()

    if(WITH_LUA)
        include_directories(../lua)

        # hvlua: standalone Lua runtime on top of libhv's event loop
        add_executable(hvlua hvlua.cpp)
        target_link_libraries(hvlua ${HV_LIBRARIES})

        list(APPEND EXAMPLES hvlua)
    endif()
if(WITH_HTTP)
    include_directories(../http)

    # wrk
    add_executable(wrk wrk.cpp)
    target_link_libraries(wrk ${HV_LIBRARIES})

    list(APPEND EXAMPLES wrk)
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
    glob_headers_and_sources(CONSUL_FILES consul)
    add_executable(consul ${CONSUL_FILES})
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
        glob_headers_and_sources(HTTPD_FILES httpd)
        add_executable(httpd ${HTTPD_FILES})
        target_link_libraries(httpd ${HV_LIBRARIES})
        list(APPEND EXAMPLES httpd)
    endif()
endif()

endif()
endif()

if(WITH_MQTT)
    include_directories(../mqtt)

    add_executable(mqtt_sub mqtt/mqtt_sub.c)
    target_link_libraries(mqtt_sub ${HV_LIBRARIES})

    add_executable(mqtt_pub mqtt/mqtt_pub.c)
    target_link_libraries(mqtt_pub ${HV_LIBRARIES})

    add_executable(mqtt_client_test mqtt/mqtt_client_test.cpp)
    target_link_libraries(mqtt_client_test ${HV_LIBRARIES})

    list(APPEND EXAMPLES mqtt_sub mqtt_pub mqtt_client_test)
endif()

# hrpc examples: TLV + protobuf + protoc-gen-hrpc codegen. Opt-in, needs protobuf.
# Kept out of the default `examples` target so the main build never requires protobuf.
option(BUILD_RPC_EXAMPLES "build hrpc examples (requires protobuf)" OFF)
if(BUILD_RPC_EXAMPLES)
    find_package(Protobuf REQUIRED)
    # Modern protobuf pulls in abseil; the plugin uses abseil hash containers
    # inline, so link the components explicitly to satisfy the linker.
    find_package(absl QUIET)

    set(RPC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../rpc)
    set(RPC_EXAMPLE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/rpc)
    set(HRPC_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/rpc_gen)
    file(MAKE_DIRECTORY ${HRPC_GEN_DIR})

    if(absl_FOUND)
        set(HRPC_ABSL_LIBS absl::flat_hash_map absl::hash absl::log_internal_check_op absl::statusor)
    endif()

    # 1) build the protoc-gen-hrpc plugin (links libprotoc)
    add_executable(protoc-gen-hrpc ${RPC_DIR}/protoc-gen-hrpc/main.cpp)
    target_include_directories(protoc-gen-hrpc PRIVATE ${Protobuf_INCLUDE_DIRS})
    target_link_libraries(protoc-gen-hrpc protobuf::libprotoc protobuf::libprotobuf ${HRPC_ABSL_LIBS})
    set_target_properties(protoc-gen-hrpc PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

    # 2) generate rpc envelope (rpc.pb.*) and calc (calc.pb.*, calc.hrpc.h)
    add_custom_command(
        OUTPUT ${HRPC_GEN_DIR}/rpc.pb.cc ${HRPC_GEN_DIR}/rpc.pb.h
        COMMAND ${Protobuf_PROTOC_EXECUTABLE}
                --cpp_out=${HRPC_GEN_DIR} -I${RPC_DIR} ${RPC_DIR}/rpc.proto
        DEPENDS ${RPC_DIR}/rpc.proto
        COMMENT "hrpc: generating rpc.pb")
    add_custom_command(
        OUTPUT ${HRPC_GEN_DIR}/calc.pb.cc ${HRPC_GEN_DIR}/calc.pb.h ${HRPC_GEN_DIR}/calc.hrpc.h
        COMMAND ${Protobuf_PROTOC_EXECUTABLE}
                --plugin=protoc-gen-hrpc=$<TARGET_FILE:protoc-gen-hrpc>
                --cpp_out=${HRPC_GEN_DIR} --hrpc_out=${HRPC_GEN_DIR}
                -I${RPC_EXAMPLE_DIR} ${RPC_EXAMPLE_DIR}/calc.proto
        DEPENDS ${RPC_EXAMPLE_DIR}/calc.proto protoc-gen-hrpc
        COMMENT "hrpc: generating calc.pb + calc.hrpc.h")

    # 3) build calc server/client (generated code requires C++17)
    add_executable(hrpc_calc_server
        rpc/rpc_calc_server.cpp ${HRPC_GEN_DIR}/calc.pb.cc ${HRPC_GEN_DIR}/rpc.pb.cc
        ${HRPC_GEN_DIR}/calc.hrpc.h)
    add_executable(hrpc_calc_client
        rpc/rpc_calc_client.cpp ${HRPC_GEN_DIR}/calc.pb.cc ${HRPC_GEN_DIR}/rpc.pb.cc
        ${HRPC_GEN_DIR}/calc.hrpc.h)
    foreach(tgt hrpc_calc_server hrpc_calc_client)
        target_include_directories(${tgt} PRIVATE ${RPC_DIR} ${HRPC_GEN_DIR} ${Protobuf_INCLUDE_DIRS})
        target_link_libraries(${tgt} ${HV_LIBRARIES} protobuf::libprotobuf ${HRPC_ABSL_LIBS})
        set_target_properties(${tgt} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    endforeach()

    list(APPEND EXAMPLES hrpc_calc_server hrpc_calc_client)
endif()

add_custom_target(examples DEPENDS ${EXAMPLES})
