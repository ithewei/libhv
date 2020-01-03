#!/bin/bash

rm -r consul
rm nohup.out
mkdir consul

print_help() {
cat <<EOF
Usage:cmd bind_ip

example:
    ./consul_agent.sh 192.168.1.123
EOF
}

main() {
    if [ $# -lt 1 ]; then
        print_help
        return
    fi
    bind_ip=$1
    nohup consul agent -server -ui -bootstrap-expect=1 -node=s1 -bind=${bind_ip} -client=0.0.0.0 -data-dir=/var/lib/consul -pid-file=consul/consul.pid -log-file=consul/consul.log &
}

main $@
