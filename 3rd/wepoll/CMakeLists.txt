cmake_minimum_required(VERSION 3.11)
project(wepoll)
include(FetchContent)
FetchContent_Declare(
  wepoll
  URL https://github.com/piscisaureus/wepoll/archive/refs/tags/v1.5.8.zip
  URL_HASH SHA1=9a145a4cf77af7ed9472859ba08fd34dd6fdd312
)
FetchContent_Populate(wepoll)
add_library(wepoll STATIC "${wepoll_SOURCE_DIR}/wepoll.c" "${wepoll_SOURCE_DIR}/wepoll.h")
set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/..")
install(TARGETS wepoll DESTINATION lib)
install(FILES "${wepoll_SOURCE_DIR}/wepoll.h" DESTINATION include/wepoll)
