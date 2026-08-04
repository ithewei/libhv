#!/bin/bash
# Generate calc.pb.* and calc.hrpc.h from calc.proto using protoc + protoc-gen-hrpc.

cd `dirname $0`

PLUGIN=../../rpc/protoc-gen-hrpc/protoc-gen-hrpc
if [ ! -x "$PLUGIN" ]; then
    echo "plugin not built; run: bash rpc/protoc-gen-hrpc/build.sh"
    exit 1
fi

# also (re)generate the rpc envelope in rpc/
protoc --cpp_out=../../rpc -I../../rpc ../../rpc/rpc.proto

set -x
protoc --plugin=protoc-gen-hrpc=$PLUGIN --cpp_out=. --hrpc_out=. -I. calc.proto
