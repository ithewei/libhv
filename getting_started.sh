#!/bin/bash

echo "Welcome to libhv!"
echo "Press any key to run ..."

echo_cmd() {
    echo -e "\n\033[36m$cmd\033[0m"
    read -n1
}

run_cmd() {
    echo_cmd
    $cmd
}

# compile httpd curl
if [ ! -x bin/httpd -o ! -x bin/curl ]; then
    cmd="make httpd curl" && echo_cmd
    ./configure
    make clean
    make -j4 httpd curl
fi

# run httpd
processes=$(ps aux | grep -v grep | grep httpd | wc -l)
if [ $processes -lt 1 ]; then
    cmd="bin/httpd -c etc/httpd.conf -s restart -d" && run_cmd
fi
ps aux | grep -v grep | grep httpd

# http file service
cmd="bin/curl -v localhost:8080" && run_cmd

# http indexof service
cmd="bin/curl -v localhost:8080/downloads/" && run_cmd
cmd="bin/curl -v localhost:8080/downloads/中文.html" && run_cmd

# http api service
cmd="bin/curl -v localhost:8080/paths" && run_cmd

cmd="bin/curl -v localhost:8080/ping" && run_cmd

cmd="bin/curl -v localhost:8080/data" && run_cmd

cmd="bin/curl -v localhost:8080/html/index.html" && run_cmd

cmd="bin/curl -v localhost:8080/get?env=1" && run_cmd

cmd="bin/curl -v localhost:8080/service" && run_cmd

cmd="bin/curl -v localhost:8080/async" && run_cmd

cmd="bin/curl -v localhost:8080/wildcard/test" && run_cmd

cmd="bin/curl -v localhost:8080/echo -d 'hello,world!'" && echo_cmd
bin/curl -v localhost:8080/echo -d 'hello,world!'

cmd="bin/curl -v localhost:8080/query?page_no=1&page_size=10" && run_cmd

cmd="bin/curl -v localhost:8080/kv   -H 'Content-Type:application/x-www-form-urlencoded' -d 'user=admin&pswd=123456'" && echo_cmd
     bin/curl -v localhost:8080/kv   -H 'Content-Type:application/x-www-form-urlencoded' -d 'user=admin&pswd=123456'

cmd="bin/curl -v localhost:8080/json -H 'Content-Type:application/json' -d '{\"user\":\"admin\",\"pswd\":\"123456\"}'" && echo_cmd
     bin/curl -v localhost:8080/json -H 'Content-Type:application/json' -d '{"user":"admin","pswd":"123456"}'

cmd="bin/curl -v localhost:8080/form -F 'user=admin' -F 'pswd=123456'" && echo_cmd
     bin/curl -v localhost:8080/form -F 'user=admin' -F 'pswd=123456'

cmd="bin/curl -v localhost:8080/upload?filename=LICENSE -d '@LICENSE'" && echo_cmd
     bin/curl -v localhost:8080/upload?filename=LICENSE -d '@LICENSE'

cmd="bin/curl -v localhost:8080/upload -F 'file=@LICENSE'" && echo_cmd
     bin/curl -v localhost:8080/upload -F 'file=@LICENSE'

cmd="bin/curl -v localhost:8080/upload/README.md -d '@README.md'" && echo_cmd
     bin/curl -v localhost:8080/upload/README.md -d '@README.md'

cmd="bin/curl -v localhost:8080/test -H 'Content-Type:application/x-www-form-urlencoded' -d 'bool=1&int=123&float=3.14&string=hello'" && echo_cmd
     bin/curl -v localhost:8080/test -H 'Content-Type:application/x-www-form-urlencoded' -d 'bool=1&int=123&float=3.14&string=hello'

cmd="bin/curl -v localhost:8080/test -H 'Content-Type:application/json' -d '{\"bool\":true,\"int\":123,\"float\":3.14,\"string\":\"hello\"}'" && echo_cmd
     bin/curl -v localhost:8080/test -H 'Content-Type:application/json' -d '{"bool":true,"int":123,"float":3.14,"string":"hello"}'

cmd="bin/curl -v localhost:8080/test -F 'bool=1' -F 'int=123' -F 'float=3.14' -F 'string=hello'" && echo_cmd
     bin/curl -v localhost:8080/test -F 'bool=1' -F 'int=123' -F 'float=3.14' -F 'string=hello'

# RESTful API: /group/:group_name/user/:user_id
cmd="bin/curl -v -X DELETE localhost:8080/group/test/user/123" && run_cmd

# show log
cmd="tail -n 50 logs/httpd*.log" && run_cmd

echo -e "\nEnjoy libhv!\n"
