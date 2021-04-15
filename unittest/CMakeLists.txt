add_definitions(-DHV_SOURCE=1)

# ------base------
add_executable(mkdir_p mkdir_test.c ../base/hbase.c)
target_include_directories(mkdir_p PRIVATE .. ../base)

add_executable(rmdir_p rmdir_test.c ../base/hbase.c)
target_include_directories(rmdir_p PRIVATE .. ../base)

add_executable(date date_test.c ../base/htime.c)
target_include_directories(date PRIVATE .. ../base)

add_executable(hatomic_test hatomic_test.c)
target_include_directories(hatomic_test PRIVATE .. ../base)
target_link_libraries(hatomic_test -lpthread)

add_executable(hthread_test hthread_test.cpp)
target_include_directories(hthread_test PRIVATE .. ../base)
target_link_libraries(hthread_test -lpthread)

add_executable(hmutex_test hmutex_test.c ../base/htime.c)
target_include_directories(hmutex_test PRIVATE .. ../base)
target_link_libraries(hmutex_test -lpthread)

add_executable(connect_test connect_test.c ../base/hsocket.c ../base/htime.c)
target_include_directories(connect_test PRIVATE .. ../base)

add_executable(socketpair_test socketpair_test.c ../base/hsocket.c)
target_include_directories(socketpair_test PRIVATE .. ../base)

# ------cpputil------
add_executable(hstring_test hstring_test.cpp ../cpputil/hstring.cpp)
target_include_directories(hstring_test PRIVATE .. ../base ../cpputil)

add_executable(ls listdir_test.cpp ../cpputil/hdir.cpp)
target_include_directories(ls PRIVATE .. ../base ../cpputil)

add_executable(ifconfig ifconfig_test.cpp ../cpputil/ifconfig.cpp)
target_include_directories(ifconfig PRIVATE .. ../base ../cpputil)

add_executable(defer_test defer_test.cpp)
target_include_directories(defer_test PRIVATE .. ../base ../cpputil)

add_executable(synchronized_test synchronized_test.cpp)
target_include_directories(synchronized_test PRIVATE .. ../base ../cpputil)
target_link_libraries(synchronized_test -lpthread)

add_executable(threadpool_test threadpool_test.cpp)
target_include_directories(threadpool_test PRIVATE .. ../base ../cpputil)
target_link_libraries(threadpool_test -lpthread)

add_executable(objectpool_test objectpool_test.cpp)
target_include_directories(objectpool_test PRIVATE .. ../base ../cpputil)
target_link_libraries(objectpool_test -lpthread)

# ------protocol------
add_executable(nslookup nslookup_test.c ../protocol/dns.c)
target_include_directories(nslookup PRIVATE .. ../base ../protocol)

add_executable(ping ping_test.c ../protocol/icmp.c ../base/hsocket.c ../base/htime.c)
target_compile_definitions(ping PRIVATE -DPRINT_DEBUG)
target_include_directories(ping PRIVATE .. ../base ../protocol)

add_executable(ftp ftp_test.c ../protocol/ftp.c ../base/hsocket.c)
target_include_directories(ftp PRIVATE .. ../base ../protocol)

add_executable(sendmail sendmail_test.c ../protocol/smtp.c ../base/hsocket.c ../util/base64.c)
target_include_directories(sendmail PRIVATE .. ../base ../protocol ../util)

if(UNIX)
add_executable(webbench webbench.c)
endif()

add_custom_target(unittest DEPENDS
    mkdir_p
    rmdir_p
    date
    hatomic_test
    hthread_test
    hmutex_test
    connect_test
    socketpair_test
    hstring_test
    ls
    ifconfig
    defer_test
    synchronized_test
    threadpool_test
    objectpool_test
    nslookup
    ping
    ftp
    sendmail
)
