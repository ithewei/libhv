#!/bin/bash

CC=gcc
CXX=g++
CFLAGS="-g -O0 -Wall"
CXXFLAGS="-std=c++11"
INCFLAGS="-I/usr/local/include/hv"
LDFLAGS="-lhv -lpthread"

# EventLoop
$CXX $CFLAGS $CXXFLAGS EventLoop_test.cpp -o EventLoop_test $INCFLAGS $LDFLAGS
$CXX $CFLAGS $CXXFLAGS EventLoopThread_test.cpp -o EventLoopThread_test $INCFLAGS $LDFLAGS
$CXX $CFLAGS $CXXFLAGS EventLoopThreadPool_test.cpp -o EventLoopThreadPool_test $INCFLAGS $LDFLAGS

# TCP
$CXX $CFLAGS $CXXFLAGS TcpServer_test.cpp -o TcpServer_test $INCFLAGS $LDFLAGS
$CXX $CFLAGS $CXXFLAGS TcpClient_test.cpp -o TcpClient_test $INCFLAGS $LDFLAGS

# UDP
$CXX $CFLAGS $CXXFLAGS UdpServer_test.cpp -o UdpServer_test $INCFLAGS $LDFLAGS
$CXX $CFLAGS $CXXFLAGS UdpClient_test.cpp -o UdpClient_test $INCFLAGS $LDFLAGS
