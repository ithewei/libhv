#!/bin/bash
#
# Validate the results written by .github/workflows/benchmark.yml.
#
# GitHub-hosted runners differ from run to run, so absolute numbers swing by
# up to 2x. Performance is therefore judged by ratios against peers measured in
# the same run (nginx, the other echo servers, a direct iperf connection),
# which stay within a narrow band. Ratios are compared at one decimal place.
#
# usage: benchmark_check.sh [result_dir]
#   result_dir holds echo.log, iperf_direct.csv, iperf_proxy.csv, wrk_<name>.log

cd "${1:-.}" || exit 1

FLOOR_ECHO=1.0          # libhv echo / median of the other echo servers
FLOOR_TCP_PROXY=0.4     # tcp_proxy_server / direct
FLOOR_HTTPD_FILE=1.2    # httpd send file (with FileCache) / nginx send file
FLOOR_TINY_FILE=0.8     # tinyhttpd send file (no FileCache) / nginx send file
FLOOR_HTTPD_404=0.8     # httpd 404 / nginx 404
FLOOR_TINY_404=0.8      # tinyhttpd 404 / nginx 404
FLOOR_HTTPD_PING=0.8    # httpd /ping / nginx /ping
FLOOR_TINY_PING=1.0     # tinyhttpd /ping / nginx /ping

failed=0
summary() { [ -n "$GITHUB_STEP_SUMMARY" ] && echo "$1" >> "$GITHUB_STEP_SUMMARY"; }
summary "| result | check | a / b | ratio | limit |"
summary "|---|---|---|---|---|"

# wrk prints "Requests/sec: R"
rps() { awk '/^Requests\/sec:/ {print $2}' "wrk_$1.log" 2>/dev/null; }

# echo-servers/benchmark.sh prints "<name> running on port P", then for each
# port "====P====" followed by "throughput = X MB/s"
echo_tp() {
    awk -v want="$1" '
        / running on port / { name[$5] = $1 }
        /^=+[0-9]+=+$/      { gsub(/=/, ""); port = $0 }
        /^throughput = /    { if (name[port] == want) print $3 }' echo.log 2>/dev/null
}

# iperf -y C: the last field is bits per second
iperf_gbps() { awk -F, 'NF > 8 {printf "%.2f\n", $NF / 1e9}' "iperf_$1.csv" 2>/dev/null | tail -1; }

median() { sort -n | awk 'NF {a[++n] = $1} END {if (n) print (n % 2) ? a[(n + 1) / 2] : (a[n / 2] + a[n / 2 + 1]) / 2}'; }

# result name a/b ratio limit
record() {
    printf "%-5s %-44s %-26s %-7s %s\n" "$1" "$2" "$3" "$4" "$5"
    summary "| $1 | $2 | $3 | $4 | $5 |"
    [ "$1" = FAIL ] && failed=$((failed + 1))
}

# name numerator denominator floor unit
check_ratio() {
    local name=$1 num=$2 den=$3 floor=$4 unit=$5 values ratio
    values="${num:-?} / ${den:-?} $unit"
    ratio=$(awk -v n="$num" -v d="$den" 'BEGIN {if (n != "" && d + 0 > 0) printf "%.1f", n / d}')
    if [ -z "$ratio" ]; then
        record FAIL "$name" "$values" "missing" ">= $floor"
    elif awk -v r="$ratio" -v f="$floor" 'BEGIN {exit !(r + 0 >= f + 0)}'; then
        record PASS "$name" "$values" "$ratio" ">= $floor"
    else
        record FAIL "$name" "$values" "$ratio" ">= $floor"
    fi
}

for f in wrk_*.log; do
    if grep -q "parse error" "$f" 2>/dev/null; then
        record FAIL "${f%.log}: http parse error" "found" "-" "none"
    fi
done

others=$(for s in libevent libev libuv asio poco; do echo_tp $s; done | median)
check_ratio "libhv echo / median"                        "$(echo_tp libhv)"      "$others"              $FLOOR_ECHO       MB/s
check_ratio "tcp proxy / direct (iperf)"                 "$(iperf_gbps proxy)"   "$(iperf_gbps direct)" $FLOOR_TCP_PROXY  Gbit/s
check_ratio "httpd file (with FileCache) / nginx file"   "$(rps httpd_file)"     "$(rps nginx_file)"    $FLOOR_HTTPD_FILE req/s
check_ratio "tinyhttpd file (no FileCache) / nginx file" "$(rps tinyhttpd_file)" "$(rps nginx_file)"    $FLOOR_TINY_FILE  req/s
check_ratio "httpd 404 / nginx 404"                      "$(rps httpd_404)"      "$(rps nginx_404)"     $FLOOR_HTTPD_404  req/s
check_ratio "tinyhttpd 404 / nginx 404"                  "$(rps tinyhttpd_404)"  "$(rps nginx_404)"     $FLOOR_TINY_404   req/s
check_ratio "httpd ping / nginx ping"                    "$(rps httpd_ping)"     "$(rps nginx_ping)"    $FLOOR_HTTPD_PING req/s
check_ratio "tinyhttpd ping / nginx ping"                "$(rps tinyhttpd_ping)" "$(rps nginx_ping)"    $FLOOR_TINY_PING  req/s

echo
if [ $failed -gt 0 ]; then
    echo "$failed check(s) failed"
    exit 1
fi
echo "all checks passed"
