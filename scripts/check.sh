#!/bin/bash

SCRIPT_DIR=$(cd `dirname $0`; pwd)
ROOT_DIR=${SCRIPT_DIR}/..
cd ${ROOT_DIR}

bin/httpd -c etc/httpd.conf -s restart -d
ps aux | grep httpd
HTTPS=`netstat -atn | grep 8443 | wc -l`

bin/http_client_test
bin/curl -v http://127.0.0.1:8080/
if [ $HTTPS -gt 0 ]; then
    bin/curl -v https://127.0.0.1:8443/
fi
