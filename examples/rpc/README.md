# hrpc example (calc)

hrpc = libhv TLV transport + protobuf + `protoc-gen-hrpc` codegen.

分层：
- `evpp/TLVMessage.h` + `evpp/TLV{Channel,Client,Server}.h`：通用 TLV 帧编解码与收发（不依赖 protobuf，可被任意二进制协议复用）。
- `rpc/`：`RpcClient` / `RpcServer` 继承 TLV 三件套，处理 protobuf 信封、服务路由、请求应答关联、超时、心跳。
- `rpc/protoc-gen-hrpc/`：protoc 插件，读 `.proto` 里的 `service` 定义，生成 `xxx.hrpc.h`（server base + client stub）。

## 构建

需要系统已安装 protobuf（`protoc` + libprotobuf + libprotoc）。

### Makefile

```bash
# homebrew 环境需指定前缀 (默认 /usr/local)
make hrpc PROTOBUF_PREFIX=/opt/homebrew
```

`make hrpc` 会依次：
1. 编译插件 `rpc/protoc-gen-hrpc/protoc-gen-hrpc`（`build.sh`，用 pkg-config 解析 protobuf/abseil 依赖）；
2. 生成 `rpc/rpc.pb.*` 与 `examples/rpc/calc.pb.*` + `examples/rpc/calc.hrpc.h`（`examples/rpc/protoc.sh`）；
3. 编译 `bin/hrpc_calc_server`、`bin/hrpc_calc_client`。

### CMake

hrpc 示例默认不构建，用 `-DBUILD_RPC_EXAMPLES=ON` 开启（会 `find_package(Protobuf)` 并在构建期编译插件、生成 stub）：

```bash
cmake .. -DBUILD_EXAMPLES=ON -DBUILD_RPC_EXAMPLES=ON \
         -DCMAKE_PREFIX_PATH=/opt/homebrew   # homebrew 环境
cmake --build . --target hrpc_calc_server hrpc_calc_client
```

现代 protobuf/abseil 头要求 C++17，故 hrpc 相关源码以 C++17 编译（TLV 三件套本身仍是 C++11、随 evpp 编译）。

## 运行

```bash
bin/hrpc_calc_server 1234
bin/hrpc_calc_client 127.0.0.1 1234
# [sync]  Add(7, 5) = 12
# [async] Sub(7, 5) = 2
```

## 自定义服务

1. 写 `.proto`，用标准 `service`：

```proto
service Calc {
    rpc Add (AddRequest) returns (AddReply);
}
```

2. 用插件生成 stub（见 `protoc.sh`）：

```bash
protoc --plugin=protoc-gen-hrpc=<plugin> --cpp_out=. --hrpc_out=. calc.proto
```

3. Server 继承生成的 `CalcService` 实现纯虚方法；Client 用生成的 `CalcStub` 调用（同步或异步）。
