#!/bin/bash

SCRIPT_DIR=$(cd `dirname $0`; pwd)
ROOT_DIR=${SCRIPT_DIR}/..

if [ $# -gt 0 ]; then
CROSS_COMPILE=$1
else
sudo apt install g++-arm-linux-gnueabi
CROSS_COMPILE=arm-linux-gnueabi-
fi
echo CROSS_COMPILE=${CROSS_COMPILE}

cd ${ROOT_DIR}
. scripts/toolchain.sh export ${CROSS_COMPILE}
BUILD_DIR=build/${HV_TARGET_OS}/${HV_TARGET_ARCH}
echo BUILD_DIR=${BUILD_DIR}
mkdir -p ${BUILD_DIR}
cd ${BUILD_DIR}
cmake ../../.. -DCMAKE_C_COMPILER=$CC -DCMAKE_CXX_COMPILER=$CXX -DCMAKE_SYSTEM_NAME=$HV_TARGET_OS -DCMAKE_SYSTEM_PROCESSOR=$HV_TARGET_ARCH
make libhv libhv_static
cd ${ROOT_DIR}
. scripts/toolchain.sh unset ${CROSS_COMPILE}

echo 'Completed => ${BUILD_DIR}'
