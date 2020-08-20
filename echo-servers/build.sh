#!/bin/bash

SCRIPT_DIR=$(cd `dirname $0`; pwd)
cd ${SCRIPT_DIR}/..

# install libevent libev libuv asio poco
UNAME=$(uname -a)
case ${UNAME} in
    *Ubuntu*|*Debian*)
        sudo apt install libevent-dev libev-dev libuv1-dev libboost-dev libboost-system-dev libasio-dev libpoco-dev;;
    *Centos*);;
    *Darwin*);;
    *);;
esac

# install muduo => https://github.com/chenshuo/muduo.git
if false; then
git clone https://github.com/chenshuo/muduo.git
pushd muduo
mkdir build && cd build
make && sudo make install
popd
fi

make libhv && sudo make install
make echo-servers
make webbench
