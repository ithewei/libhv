#!/bin/bash

SCRIPT_DIR=$(cd `dirname $0`; pwd)
ROOT_DIR=${SCRIPT_DIR}/..
export DYLD_LIBRARY_PATH=${ROOT_DIR}/lib:${DYLD_LIBRARY_PATH}
export LD_LIBRARY_PATH=${ROOT_DIR}/lib:${LD_LIBRARY_PATH}
cd ${ROOT_DIR}

bin/rbtree_test
bin/hbase_test
bin/date
bin/ifconfig
bin/mkdir_p 123/456
bin/ls
bin/rmdir_p 123/456
bin/hlog_test

bin/base64
bin/md5
bin/sha1

bin/defer_test
bin/hstring_test
bin/hpath_test
bin/hurl_test
# bin/hatomic_test
# bin/hatomic_cpp_test
# bin/hthread_test
# bin/hmutex_test
bin/socketpair_test
# bin/threadpool_test
# bin/objectpool_test
bin/sizeof_test
bin/http_router_test
if [ -x bin/hdns_test ]; then
    bin/hdns_test
fi
if [ -x bin/tcpclient_dns_test ]; then
    bin/tcpclient_dns_test
fi
if [ -x bin/asynchttp_dns_test ]; then
    bin/asynchttp_dns_test
fi
if [ -x bin/websocket_dns_test ]; then
    bin/websocket_dns_test
fi
for redis_test in redis_async_client_test redis_client_test redis_batch_test redis_subscriber_test; do
    if [ -x bin/${redis_test} ]; then
        bin/${redis_test}
    fi
done
if [ -x bin/redis_protocol_test ]; then
    bin/redis_protocol_test
fi
