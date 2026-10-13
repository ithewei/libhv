#!/bin/bash
#
# Validate the results written by .github/workflows/benchmark.yml.
#
# GitHub-hosted runners differ from run to run, so absolute numbers swing by
# up to 2x. Performance is therefore judged by ratios against peers measured in
# the same run (nginx, other echo servers), which stay within a narrow band.
# Floors are ~85% of the lowest ratio seen across 27 recent runs.
#
# usage: benchmark_check.sh [result_dir]
#   result_dir holds echo.log, iperf_direct.csv, iperf_proxy.csv, wrk_<name>.log

cd "${1:-.}" || exit 1

# wrk runs with this many connections; at most one request per connection may
# be in flight when the test stops.
MAX_UNANSWERED=110

FLOOR_ECHO=0.84         # libhv echo / median of the other echo servers
FLOOR_HTTPD_FILE=1.13   # httpd send file / nginx send file
FLOOR_HTTPD_404=0.81    # httpd 404 / nginx 404
FLOOR_TINY_404=0.87     # tinyhttpd 404 / nginx 404
FLOOR_HTTPD_PING=0.89   # httpd /ping / nginx 404
FLOOR_TINY_PING=0.92    # tinyhttpd /ping / nginx 404
FLOOR_TCP_PROXY=0.38    # tcp_proxy_server / direct (warn only until more data)

failed=0
summary() { [ -n "$GITHUB_STEP_SUMMARY" ] && echo "$1" >> "$GITHUB_STEP_SUMMARY"; }
summary "| result | check | value | limit |"
summary "|---|---|---|---|"

# wrk prints "N requests, M OK, ..." and "Requests/sec: R"
rps()        { awk '/^Requests\/sec:/ {print $2}' "wrk_$1.log" 2>/dev/null; }
unanswered() { awk '/ requests, [0-9]+ OK/ {print $1 - $3}' "wrk_$1.log" 2>/dev/null; }

# echo-servers/benchmark.sh prints "<name> running on port P", then for each
# port "====P====" followed by "throughput = X MB/s"
echo_tp() {
    awk -v want="$1" '
        / running on port / { name[$5] = $1 }
        /^=+[0-9]+=+$/      { gsub(/=/, ""); port = $0 }
        /^throughput = /    { if (name[port] == want) print $3 }' echo.log 2>/dev/null
}

# iperf -y C: the last field is bits per second
iperf_bps() { awk -F, 'NF > 8 {print $NF}' "iperf_$1.csv" 2>/dev/null | tail -1; }

median() { sort -n | awk 'NF {a[++n] = $1} END {if (n) print (n % 2) ? a[(n + 1) / 2] : (a[n / 2] + a[n / 2 + 1]) / 2}'; }

# result name value limit
record() {
    printf "%-5s %-44s %-8s %s\n" "$1" "$2" "$3" "$4"
    summary "| $1 | $2 | $3 | $4 |"
    [ "$1" = FAIL ] && failed=$((failed + 1))
}

# name numerator denominator floor [warn|info]
check_ratio() {
    local name=$1 num=$2 den=$3 floor=$4 mode=$5
    if [ -z "$num" ] || [ -z "$den" ]; then
        record FAIL "$name" "missing" ">= $floor"
        return
    fi
    local ratio ok
    ratio=$(awk -v n="$num" -v d="$den" 'BEGIN {if (d + 0 > 0) printf "%.2f", n / d}')
    ok=$(awk -v r="$ratio" -v f="$floor" 'BEGIN {print (r != "" && r + 0 >= f) ? 1 : 0}')
    if [ "$mode" = info ]; then
        record INFO "$name" "$ratio" "-"
    elif [ "$ok" = 1 ]; then
        record PASS "$name" "$ratio" ">= $floor"
    elif [ "$mode" = warn ]; then
        record WARN "$name" "$ratio" ">= $floor"
    else
        record FAIL "$name" "$ratio" ">= $floor"
    fi
}

# name value max
check_max() {
    if [ -z "$2" ]; then
        record FAIL "$1" "missing" "<= $3"
    elif [ "$2" -le "$3" ]; then
        record PASS "$1" "$2" "<= $3"
    else
        record FAIL "$1" "$2" "<= $3"
    fi
}

echo "== correctness =="
for s in libevent libev libuv libhv asio poco; do
    tp=$(echo_tp $s)
    if [ -n "$tp" ] && [ "$tp" -gt 0 ]; then
        record PASS "echo server $s" "${tp}MB/s" "> 0"
    else
        record FAIL "echo server $s" "${tp:-missing}" "> 0"
    fi
done
for t in httpd_file tinyhttpd_file httpd_ping tinyhttpd_ping; do
    check_max "$t unanswered requests" "$(unanswered $t)" $MAX_UNANSWERED
done
for f in wrk_*.log; do
    if grep -q "parse error" "$f" 2>/dev/null; then
        record FAIL "${f%.log}: http parse error" "found" "none"
    fi
done

echo "== performance (same-run ratios) =="
others=$(for s in libevent libev libuv asio poco; do echo_tp $s; done | median)
check_ratio "libhv echo / median(other echo servers)" "$(echo_tp libhv)"       "$others"             $FLOOR_ECHO
check_ratio "httpd file / nginx file"                 "$(rps httpd_file)"      "$(rps nginx_file)"   $FLOOR_HTTPD_FILE
check_ratio "httpd 404 / nginx 404"                   "$(rps httpd_404)"       "$(rps nginx_404)"    $FLOOR_HTTPD_404
check_ratio "tinyhttpd 404 / nginx 404"               "$(rps tinyhttpd_404)"   "$(rps nginx_404)"    $FLOOR_TINY_404
check_ratio "httpd /ping / nginx 404"                 "$(rps httpd_ping)"      "$(rps nginx_404)"    $FLOOR_HTTPD_PING
check_ratio "tinyhttpd /ping / nginx 404"             "$(rps tinyhttpd_ping)"  "$(rps nginx_404)"    $FLOOR_TINY_PING
check_ratio "tcp proxy / direct (iperf)"              "$(iperf_bps proxy)"     "$(iperf_bps direct)" $FLOOR_TCP_PROXY warn
check_ratio "tinyhttpd file / nginx file"             "$(rps tinyhttpd_file)"  "$(rps nginx_file)"   0 info

echo
if [ $failed -gt 0 ]; then
    echo "$failed check(s) failed"
    exit 1
fi
echo "all checks passed"
