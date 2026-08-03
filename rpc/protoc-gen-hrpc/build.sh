#!/bin/bash
# Build the protoc-gen-hrpc plugin using the system protobuf/protoc toolchain.
# Requires: protoc, libprotoc, libprotobuf (and abseil on modern protobuf).

cd `dirname $0`

CXX=${CXX:-g++}

# Prefer pkg-config to resolve protobuf + abseil dependency chain.
if pkg-config --exists protobuf 2>/dev/null; then
    PB_CFLAGS=$(pkg-config --cflags protobuf)
    PB_LIBS=$(pkg-config --libs protobuf)
else
    PB_CFLAGS=""
    PB_LIBS="-lprotobuf"
fi

set -x
$CXX -std=c++17 $PB_CFLAGS main.cpp -o protoc-gen-hrpc $PB_LIBS -lprotoc
