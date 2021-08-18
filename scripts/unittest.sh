#!/bin/bash

SCRIPT_DIR=$(cd `dirname $0`; pwd)
ROOT_DIR=${SCRIPT_DIR}/..
cd ${ROOT_DIR}

bin/date
bin/ifconfig
bin/mkdir_p 123/456
bin/ls
bin/rmdir_p 123/456

bin/defer_test
bin/hstring_test
# bin/hatomic_test
# bin/hatomic_cpp_test
# bin/hthread_test
# bin/hmutex_test
bin/socketpair_test
# bin/threadpool_test
# bin/objectpool_test
