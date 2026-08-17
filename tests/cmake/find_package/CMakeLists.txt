cmake_minimum_required(VERSION 3.10)
project(libhv_find_package_test LANGUAGES C)

find_package(libhv 1.3.4 CONFIG REQUIRED)

add_executable(libhv_find_package_test main.c)
target_link_libraries(libhv_find_package_test PRIVATE libhv::hv)
