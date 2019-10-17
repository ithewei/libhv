#!/bin/bash

make httpd curl

bin/httpd -d
ps aux | grep httpd

# http web service
bin/curl -v localhost:8080

# indexof
bin/curl -v localhost:8080/downloads/

# http api service
bin/curl -v -X POST localhost:8080/v1/api/json -H "Content-Type:application/json" -d '{"user":"admin","pswd":"123456"}'
