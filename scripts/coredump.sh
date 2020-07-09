#!/bin/bash

print_conf() {
    echo /proc/sys/kernel/core_uses_pid
    cat /proc/sys/kernel/core_uses_pid
    echo /proc/sys/kernel/core_pattern
    cat /proc/sys/kernel/core_pattern
    echo /proc/sys/fs/suid_dumpable
    cat /proc/sys/fs/suid_dumpable
}

print_conf
echo "1" > /proc/sys/kernel/core_uses_pid
echo "/tmp/core-%e-%p-%t" > /proc/sys/kernel/core_pattern
echo "1" > /proc/sys/fs/suid_dumpable
print_conf

ulimit -c unlimited
ulimit -a
