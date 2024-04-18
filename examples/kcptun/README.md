# Intro

<img src="kcptun.png" alt="kcptun" height="300px"/>

> *Disclaimer: The picture comes from [github.com/xtaci/kcptun](https://github.com/xtaci/kcptun). Thanks so much.*

# Build

```shell
./configure --with-kcp
make clean
make examples
make kcptun
```

# Usage

```shell
$ bin/kcptun_server -h

Usage: kcptun_server [hvdl:t:m:]
Options:

  -h|--help                 Print this information
  -v|--version              Print version
  -d|--daemon               Daemonize
  -l|--listen value         kcp server listen address (default: ":4000")
  -t|--target value         target server address (default: "127.0.0.1:8080")
  -m|--mode value           profiles: fast3, fast2, fast, normal (default: "fast")
     --mtu value            set maximum transmission unit for UDP packets (default: 1350)
     --sndwnd value         set send window size(num of packets) (default: 1024)
     --rcvwnd value         set receive window size(num of packets) (default: 1024)
```

```shell
$ bin/kcptun_client -h

Usage: kcptun_client [hvdl:r:m:]
Options:

  -h|--help                 Print this information
  -v|--version              Print version
  -d|--daemon               Daemonize
  -l|--localaddr value      local listen address (default: ":8388")
  -r|--remoteaddr value     kcp server address (default: "127.0.0.1:4000")
  -m|--mode value           profiles: fast3, fast2, fast, normal (default: "fast")
     --mtu value            set maximum transmission unit for UDP packets (default: 1350)
     --sndwnd value         set send window size(num of packets) (default: 128)
     --rcvwnd value         set receive window size(num of packets) (default: 512)
```

# Test
`tcp_client -> kcptun_client -> kcptun_server -> tcp_server`
```shell
tcp_server:     bin/tcp_echo_server 1234
kcptun_server:  bin/kcptun_server -l :4000 -t 127.0.0.1:1234 --mode fast3
kcptun_client:  bin/kcptun_client -l :8388 -r 127.0.0.1:4000 --mode fast3
tcp_client:     bin/nc 127.0.0.1 8388
                > hello
                < hello
```

This kcptun examples does not implement encryption, compression, and fec.<br>
if you want to use [github.com/xtaci/kcptun](https://github.com/xtaci/kcptun), please add `--crypt null --nocomp --ds 0 --ps 0`.<br>
For example:
```shell
golang_kcptun_server -l :4000 -t 127.0.0.1:1234 --mode fast3 --crypt null --nocomp --ds 0 --ps 0
```
