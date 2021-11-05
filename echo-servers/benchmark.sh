#!/bin/bash

host=127.0.0.1
port=2000
connections=100
duration=10
threads=2
sendbytes=1024

while getopts 'h:p:c:d:t:' opt
do
    case $opt in
        h) host=$OPTARG;;
        p) port=$OPTARG;;
        c) connections=$OPTARG;;
        d) duration=$OPTARG;;
        t) threads=$OPTARG;;
        *) exit -1;;
    esac
done

SCRIPT_DIR=$(cd `dirname $0`; pwd)
cd ${SCRIPT_DIR}/..

killall_echo_servers() {
    #sudo killall libevent_echo libev_echo libuv_echo libhv_echo asio_echo poco_echo muduo_echo
    if [ $(ps aux | grep _echo | grep -v grep | wc -l) -gt 0 ]; then
        ps aux | grep _echo | grep -v grep | awk '{print $2}' | xargs sudo kill -9
    fi
}

export LD_LIBRARY_PATH=lib:$LD_LIBRARY_PATH

killall_echo_servers

sport=$port

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

for ((p=$sport+1; p<=$port; ++p)); do
    echo -e "\n==============$p====================================="
    bin/pingpong_client -H $host -p $p -c $connections -d $duration -t $threads -b $sendbytes
    sleep 1
done

killall_echo_servers
