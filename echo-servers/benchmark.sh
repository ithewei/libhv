#! /bin/bash

killall_echo_servers() {
    #sudo killall libevent_echo libev_echo libuv_echo libhv_echo asio_echo poco_echo muduo_echo
    ps aux | grep _echo | grep -v grep | awk '{print $2}' | xargs sudo kill -9
}

export LD_LIBRARY_PATH=lib:$LD_LIBRARY_PATH

ip=127.0.0.1
sport=2000
port=$sport

killall_echo_servers

if [ -x bin/libevent_echo ]; then
    let port++
    bin/libevent_echo $port &
    echo "libevent running on port $port"
fi

if [ -x bin/libev_echo ]; then
    let port++
    bin/libev_echo $port &
    echo "libev running on port $port"
fi

if [ -x bin/libuv_echo ]; then
    let port++
    bin/libuv_echo $port &
    echo "libuv running on port $port"
fi

if [ -x bin/libhv_echo ]; then
    let port++
    bin/libhv_echo $port &
    echo "libhv running on port $port"
fi

if [ -x bin/asio_echo ]; then
    let port++
    bin/asio_echo $port &
    echo "asio running on port $port"
fi

if [ -x bin/poco_echo ]; then
    let port++
    bin/poco_echo $port &
    echo "poco running on port $port"
fi

if [ -x bin/muduo_echo ]; then
    let port++
    bin/muduo_echo $port &
    echo "muduo running on port $port"
fi

sleep 1

client=2
time=60
if [ $# -gt 0 ]; then
    time=$1
fi
for ((p=$sport+1; p<=$port; ++p)); do
    echo -e "\n==============$p====================================="
    bin/webbench -q -c $client -t $time $ip:$p
    sleep 1
done

killall_echo_servers
