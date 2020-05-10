#!/bin/bash

if [ ! -x bin/httpd -o ! -x bin/curl ]; then
    make httpd curl
fi

processes=$(ps aux | grep -v grep | grep httpd | wc -l)
if [ $processes -lt 1 ]; then
    bin/httpd -s restart -d
fi
ps aux | grep httpd

PS4="\033[32m+ \033[0m"
set -x

# http web service
read -n1
bin/curl -v localhost:8080

# http indexof service
read -n1
bin/curl -v localhost:8080/downloads/

# http api service
read -n1
bin/curl -v localhost:8080/v1/api/echo -d "hello,world!"

read -n1
bin/curl -v localhost:8080/v1/api/query?page_no=1\&page_size=10

read -n1
bin/curl -v localhost:8080/v1/api/kv   -H "Content-Type:application/x-www-form-urlencoded" -d 'user=admin&pswd=123456'

read -n1
bin/curl -v localhost:8080/v1/api/json -H "Content-Type:application/json" -d '{"user":"admin","pswd":"123456"}'

read -n1
bin/curl -v localhost:8080/v1/api/form -F "user=admin pswd=123456"

read -n1
bin/curl -v localhost:8080/v1/api/upload -F "file=@LICENSE"

read -n1
bin/curl -v localhost:8080/v1/api/test -H "Content-Type:application/x-www-form-urlencoded" -d 'bool=1&int=123&float=3.14&string=hello'

read -n1
bin/curl -v localhost:8080/v1/api/test -H "Content-Type:application/json" -d '{"bool":true,"int":123,"float":3.14,"string":"hello"}'

read -n1
bin/curl -v localhost:8080/v1/api/test -F 'bool=1 int=123 float=3.14 string=hello'

# RESTful API: /group/:group_name/user/:user_id
read -n1
bin/curl -v -X DELETE localhost:8080/v1/api/group/test/user/123

