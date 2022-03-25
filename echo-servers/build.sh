#!/bin/bash

SCRIPT_DIR=$(cd `dirname $0`; pwd)
ROOT_DIR=${SCRIPT_DIR}/..

# install libevent libev libuv asio poco
UNAME=$(uname -a)
case ${UNAME} in
    *Ubuntu*|*Debian*)
        sudo apt install libevent-dev libev-dev libuv1-dev libboost-dev libboost-system-dev libasio-dev libpoco-dev
        ;;
    *CentOS*)
        sudo yum install libevent-devel libev-devel libuv-devel boost-devel asio-devel poco-devel
        ;;
    *Darwin*)
        brew install libevent libev libuv boost asio poco
        ;;
    *)
        echo 'please install libevent libev libuv boost asio poco'
        ;;
esac

# install muduo => https://github.com/chenshuo/muduo.git
TEST_MUDUO=false
if [ "$TEST_MUDUO" == "true" ]; then
    cd ${ROOT_DIR}/..
    git clone https://github.com/chenshuo/muduo.git
    cd muduo
    mkdir build && cd build
    cmake .. && make && sudo make install
fi

# install libhv
cd ${ROOT_DIR}
make libhv && sudo make install && sudo ldconfig

# build echo-servers
make echo-servers
