#!/bin/bash

SCRIPT_DIR=$(cd `dirname $0`; pwd)
ROOT_DIR=${SCRIPT_DIR}/..
cd ${ROOT_DIR}

bin/httpd -c etc/httpd.conf -s restart -d

bin/ls
bin/date
bin/ifconfig
bin/mkdir_p 123/456
bin/rmdir_p 123/456

bin/defer_test
bin/hstring_test
bin/hpath_test
bin/hatomic_test
bin/hatomic_cpp_test
bin/hmutex_test
bin/socketpair_test
bin/threadpool_test
bin/objectpool_test

bin/curl -v localhost:8080
bin/curl -v localhost:8080/ping
bin/curl -v localhost:8080/echo -d "hello,world!"
bin/curl -v localhost:8080/query?page_no=1\&page_size=10
bin/curl -v localhost:8080/kv   -H "Content-Type:application/x-www-form-urlencoded" -d 'user=admin&pswd=123456'
bin/curl -v localhost:8080/json -H "Content-Type:application/json" -d '{"user":"admin","pswd":"123456"}'
bin/curl -v localhost:8080/form -F "user=admin pswd=123456"
bin/curl -v localhost:8080/upload -F "file=@LICENSE"
bin/curl -v localhost:8080/test -H "Content-Type:application/x-www-form-urlencoded" -d 'bool=1&int=123&float=3.14&string=hello'
bin/curl -v localhost:8080/test -H "Content-Type:application/json" -d '{"bool":true,"int":123,"float":3.14,"string":"hello"}'
bin/curl -v localhost:8080/test -F 'bool=1 int=123 float=3.14 string=hello'
bin/curl -v -X DELETE localhost:8080/group/test/user/123

bin/httpd -s stop

bin/htimer_test
