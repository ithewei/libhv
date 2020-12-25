#!/bin/bash

g++ -std=c++11 EventLoop_test.cpp -o EventLoop_test -I/usr/local/include/hv -lhv
g++ -std=c++11 EventLoopThread_test.cpp -o EventLoopThread_test -I/usr/local/include/hv -lhv -lpthread
g++ -std=c++11 EventLoopThreadPool_test.cpp -o EventLoopThreadPool_test -I/usr/local/include/hv -lhv -lpthread
