# hrpc: TLV + protobuf RPC, built as a separate library (libhrpc) so libhv
# stays protobuf-free. Headers install to include/hv/rpc.
# Added from the top-level CMakeLists via add_subdirectory(rpc) when WITH_RPC=ON.

find_package(Protobuf REQUIRED)
find_package(absl QUIET)
if(absl_FOUND)
    set(HRPC_ABSL_LIBS absl::flat_hash_map absl::hash absl::log_internal_check_op absl::statusor)
endif()

set(RPC_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(HV_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(HRPC_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/gen)
file(MAKE_DIRECTORY ${HRPC_GEN_DIR})

# protoc plugin (links libprotoc)
add_executable(protoc-gen-hrpc ${RPC_DIR}/protoc-gen-hrpc/main.cpp)
target_include_directories(protoc-gen-hrpc PRIVATE ${Protobuf_INCLUDE_DIRS})
target_link_libraries(protoc-gen-hrpc protobuf::libprotoc protobuf::libprotobuf ${HRPC_ABSL_LIBS})
set_target_properties(protoc-gen-hrpc PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

# generate the rpc envelope (rpc.pb.*)
add_custom_command(
    OUTPUT ${HRPC_GEN_DIR}/rpc.pb.cc ${HRPC_GEN_DIR}/rpc.pb.h
    COMMAND ${Protobuf_PROTOC_EXECUTABLE} --cpp_out=${HRPC_GEN_DIR} -I${RPC_DIR} ${RPC_DIR}/rpc.proto
    DEPENDS ${RPC_DIR}/rpc.proto
    COMMENT "hrpc: generating rpc.pb")

# the hrpc library: RpcClient/RpcServer + envelope; links libhv + protobuf
set(HRPC_SRCS ${RPC_DIR}/RpcClient.cpp ${RPC_DIR}/RpcServer.cpp ${HRPC_GEN_DIR}/rpc.pb.cc)
add_library(hrpc ${HRPC_SRCS})
target_include_directories(hrpc PRIVATE
    ${HV_ROOT} ${HV_ROOT}/base ${HV_ROOT}/ssl ${HV_ROOT}/event ${HV_ROOT}/cpputil ${HV_ROOT}/evpp
    ${RPC_DIR} ${HRPC_GEN_DIR} ${Protobuf_INCLUDE_DIRS}
    INTERFACE $<BUILD_INTERFACE:${HV_ROOT}/include>
              $<BUILD_INTERFACE:${HRPC_GEN_DIR}> $<INSTALL_INTERFACE:include/hv/rpc>)
target_link_libraries(hrpc ${HV_LIBRARIES} protobuf::libprotobuf ${HRPC_ABSL_LIBS})
set_target_properties(hrpc PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
add_custom_target(libhrpc DEPENDS hrpc)

# install rpc headers to include/hv/rpc + the generated envelope header + plugin
install(FILES ${RPC_DIR}/rpc_protocol.h ${RPC_DIR}/RpcService.h
              ${RPC_DIR}/RpcClient.h ${RPC_DIR}/RpcServer.h ${HRPC_GEN_DIR}/rpc.pb.h
        DESTINATION include/hv/rpc)
install(TARGETS hrpc ARCHIVE DESTINATION lib LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
install(TARGETS protoc-gen-hrpc RUNTIME DESTINATION bin)

# exported for examples: link libs + the generated-header dir (for calc stubs)
set(HRPC_LIBRARIES hrpc CACHE INTERNAL "link hrpc libraries")
set(HRPC_ABSL_LIBS "${HRPC_ABSL_LIBS}" CACHE INTERNAL "hrpc abseil libs")
