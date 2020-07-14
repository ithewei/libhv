add_definitions(-DHV_SOURCE=1)

aux_source_directory(../base        BASE_SRCS)
aux_source_directory(../utils       UTILS_SRCS)
aux_source_directory(../event       EVENT_SRCS)
aux_source_directory(../http        HTTP_SRCS)
aux_source_directory(../http/client HTTP_CLIENT_SRCS)
aux_source_directory(../http/server HTTP_SERVER_SRCS)
aux_source_directory(../consul      CONSUL_SRCS)
aux_source_directory(httpd          HTTPD_SRCS)

add_executable(hmain_test hmain_test.cpp ${BASE_SRCS} ${UTILS_SRCS})
target_include_directories(hmain_test PRIVATE .. ../base ../utils)
target_link_libraries(hmain_test ${LIBS})

add_executable(htimer_test htimer_test.c ${BASE_SRCS} ${EVENT_SRCS})
target_include_directories(htimer_test PRIVATE .. ../base ../event)
target_link_libraries(htimer_test ${LIBS})

add_executable(hloop_test hloop_test.c ${BASE_SRCS} ${EVENT_SRCS})
target_include_directories(hloop_test PRIVATE .. ../base ../event)
target_link_libraries(hloop_test ${LIBS})

add_executable(tcp tcp.c ${BASE_SRCS} ${EVENT_SRCS})
target_include_directories(tcp PRIVATE .. ../base ../event)
target_link_libraries(tcp ${LIBS})

add_executable(udp udp.c ${BASE_SRCS} ${EVENT_SRCS})
target_include_directories(udp PRIVATE .. ../base ../event)
target_link_libraries(udp ${LIBS})

add_executable(nc nc.c ${BASE_SRCS} ${EVENT_SRCS})
target_include_directories(nc PRIVATE .. ../base ../event)
target_link_libraries(nc ${LIBS})

add_executable(nmap nmap.cpp ${BASE_SRCS} ${EVENT_SRCS})
target_compile_definitions(nmap PRIVATE -DPRINT_DEBUG)
target_include_directories(nmap PRIVATE .. ../base ../event)
target_link_libraries(nmap ${LIBS})

add_executable(httpd ${BASE_SRCS} ${UTILS_SRCS} ${EVENT_SRCS} ${HTTP_SRCS} ${HTTP_SERVER_SRCS} ${HTTPD_SRCS})
target_include_directories(httpd PRIVATE .. ../base ../utils ../event ../http ../http/server httpd)
target_link_libraries(httpd ${LIBS})

add_executable(curl curl.cpp ${BASE_SRCS} ${UTILS_SRCS} ${HTTP_SRCS} ${HTTP_CLIENT_SRCS})
# target_compile_definitions(curl PRIVATE -DWITH_CURL -DCURL_STATICLIB)
target_include_directories(curl PRIVATE .. ../base ../utils ../http ../http/client)
target_link_libraries(curl ${LIBS})

add_executable(consul_cli consul_cli.cpp ${BASE_SRCS} ${UTILS_SRCS} ${HTTP_SRCS} ${HTTP_CLIENT_SRCS} ${CONSUL_SRCS})
target_compile_definitions(consul_cli PRIVATE PRINT_DEBUG)
target_include_directories(consul_cli PRIVATE .. ../base ../utils ../http ../http/client ../consul)
target_link_libraries(consul_cli ${LIBS})

add_custom_target(examples DEPENDS
    hmain_test
    htimer_test
    hloop_test
    tcp
    udp
    nc
    nmap
    httpd
    curl
    consul_cli
)
