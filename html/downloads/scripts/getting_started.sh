#!/bin/bash

make httpd curl

bin/httpd -s restart -d
ps aux | grep httpd

# http web service
bin/curl -v localhost:8080

# http indexof service
bin/curl -v localhost:8080/downloads/

# http api service
bin/curl -v localhost:8080/v1/api/hello
bin/curl -v localhost:8080/v1/api/echo -d "hello,world!"
bin/curl -v localhost:8080/v1/api/query?page_no=1&page_size=10
bin/curl -v localhost:8080/v1/api/kv   -H "Content-Type:application/x-www-form-urlencoded" -d 'user=admin&pswd=123456'
bin/curl -v localhost:8080/v1/api/json -H "Content-Type:application/json" -d '{"user":"admin","pswd":"123456"}'
bin/curl -v localhost:8080/v1/api/form -F "file=@LICENSE"
# RESTful API: /group/:group_name/user/:user_id
bin/curl -v -X DELETE localhost:8080/v1/api/group/test/user/123
