# URMA RPC 使用说明

URMA 是 `coro_rpc` 的可选传输层。使用前请确认运行环境已安装 URMA 用户态库、头文件和对应驱动。

## 构建

```bash
cmake -S . -B build -DYLT_ENABLE_URMA=ON -DBUILD_EXAMPLES=ON
cmake --build build --target coro_rpc_urma_example -j
```

## 最小示例

服务端：

```cpp
coro_rpc_server server(std::thread::hardware_concurrency(), 9000);
server.init_urma();
server.register_handler<echo>();
server.start();
```

客户端在连接前初始化 URMA：

```cpp
coro_rpc_client client;
if (!client.init_urma()) {
  return;
}
auto ec = co_await client.connect("127.0.0.1:9000");
auto result = co_await client.call_for<echo>(30s, "hello urma");
```

不调用 `init_urma()` 时，`coro_rpc` 默认使用 TCP。

## 配置

服务端和客户端可以传入同一个 `coro_io::urma_socket_t::config_t`：

```cpp
coro_io::urma_socket_t::config_t config{
    .cq_size = 128,
    .recv_buffer_cnt = 64,
    .send_buffer_cnt = 64,
    .buffer_size = 4096,
    .max_memory_usage = 256ull * 1024 * 1024,
    .device_name = "bonding_dev_0",
    .eid_index = 0,
    .tp_type = URMA_CTP,
};

server.init_urma(config);
client.init_urma(config);
```

`device_name` 为空时自动选择设备。`buffer_size`、队列深度和内存上限应根据设备能力及并发量设置；CTP 场景通常使用 4 KiB 分片。

## 大数据与压测

大 payload 建议使用 RPC attachment，避免把数据重复放入普通 RPC 参数：

```cpp
client.set_req_attachment(payload);
auto result = co_await client.call_for<upload>(30s, payload.size());
```

构建压测工具：

```bash
cmake --build build --target coro_rpc_urma_benchmark -j
```

运行服务端和客户端：

```bash
./build/output/examples/coro_rpc/coro_rpc_urma_benchmark server \
  --host 0.0.0.0 --port 9001 --device bonding_dev_0

./build/output/examples/coro_rpc/coro_rpc_urma_benchmark client \
  --host <server-ip> --port 9001 --device bonding_dev_0 \
  --mode both --payload 64 --connections 128 --duration 30
```

需要排除 RPC 开销时使用 `--transport raw`；需要测试 attachment 快路径时使用 `--rpc attach_sink`。

## 排查

- 找不到 URMA 目标：确认重新执行了带 `-DYLT_ENABLE_URMA=ON` 的 CMake 配置。
- 初始化失败：检查设备名、EID、驱动和用户态库是否匹配。
- 连接失败：确认 TCP 握手端口可达，且两端都已调用 `init_urma()`。
- 高并发下出现发送或接收错误：降低连接数、pipeline depth 或队列深度，并适当增加内存上限。

完整示例见 `src/coro_rpc/examples/urma_example/urma_example.cpp`，压测参数以 `src/coro_rpc/examples/urma_benchmark/urma_benchmark.cpp` 为准。
