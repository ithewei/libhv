#!/bin/bash

print_help() {
cat <<EOF
Usage: command

command:
    export CROSS_COMPILE
    unset

example:
    source ./toolchain.sh export arm-linux-androideabi
    source ./toolchain.sh unset

EOF
}

main() {
    if [ $# -lt 1 ]; then
        print_help
        return
    fi
    COMMAND=$1

    if [ $COMMAND = "export" ]; then
        if [ $# -lt 2 ]; then
            print_help
            return
        fi
        CROSS_COMPILE=$2
        if [ ${CROSS_COMPILE:${#CROSS_COMPILE}-1:1} != "-" ]; then
            CROSS_COMPILE=${CROSS_COMPILE}-
        fi
        echo "CROSS_COMPILE=$CROSS_COMPILE"
        export CROSS_COMPILE=${CROSS_COMPILE}
        export CC=${CROSS_COMPILE}gcc
        export CXX=${CROSS_COMPILE}g++
        export AR=${CROSS_COMPILE}ar
        export AS=${CROSS_COMPILE}as
        export LD=${CROSS_COMPILE}ld
        export STRIP=${CROSS_COMPILE}strip
        export RANLIB=${CROSS_COMPILE}ranlib
        export NM=${CROSS_COMPILE}nm

        HOST_OS=`uname -s`
        HOST_ARCH=`uname -m`
        TARGET_PLATFORM=`$CC -v 2>&1 | grep Target | sed 's/Target: //'`
        TARGET_ARCH=`echo $TARGET_PLATFORM | awk -F'-' '{print $1}'`

        case $TARGET_PLATFORM in
            *mingw*) TARGET_OS=Windows ;;
            *android*) TARGET_OS=Android ;;
            *darwin*) TARGET_OS=Darwin ;;
            *) TARGET_OS=Linux ;;
        esac
        # TARGET_OS,TARGET_ARCH used by make
        export HV_HOST_OS=$HOST_OS
        export HV_HOST_ARCH=$HOST_ARCH
        export HV_TARGET_OS=$TARGET_OS
        export HV_TARGET_ARCH=$TARGET_ARCH
        export HOST=$TARGET_PLATFORM
    elif [ $COMMAND = "unset" ]; then
        unset  CROSS_COMPILE
        unset  CC
        unset  CXX
        unset  AR
        unset  AS
        unset  LD
        unset  STRIP
        unset  RANLIB
        unset  NM

        unset  HOST_OS
        unset  HOST_ARCH
        unset  TARGET_OS
        unset  TARGET_ARCH
        unset  HOST
    else
        print_help
        return
    fi
}

main $@
echo "CC     =   $CC"
echo "CXX    =   $CXX"
if [ $CC ]; then
CC_VERSION=`$CC --version 2>&1 | head -n 1`
echo "$CC_VERSION"
fi
echo "AR     =   $AR"
echo "AS     =   $AS"
echo "LD     =   $LD"
echo "STRIP  =   $STRIP"
echo "RANLIB =   $RANLIB"
echo "NM     =   $NM"

echo "HV_HOST_OS     = $HOST_OS"
echo "HV_HOST_ARCH   = $HOST_ARCH"
echo "HV_TARGET_OS   = $TARGET_OS"
echo "HV_TARGET_ARCH = $TARGET_ARCH"
