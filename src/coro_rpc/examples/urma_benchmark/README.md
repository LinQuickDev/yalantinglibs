# URMA RPC Benchmark

`coro_rpc_urma_benchmark` is a small benchmark tool for URMA-based coro_rpc.
It can run on the same node or across two nodes, and supports low-load latency
and throughput tests.

## Build

Configure with URMA enabled and examples enabled:

```bash
cmake -S . -B build -DYLT_ENABLE_URMA=ON -DBUILD_EXAMPLES=ON
```

Build only this benchmark target:

```bash
cmake --build build --target coro_rpc_urma_benchmark -j
```

The binary is generated at:

```bash
build/output/examples/coro_rpc/coro_rpc_urma_benchmark
```

If the target prints this message:

```text
coro_rpc_urma_benchmark requires -DYLT_ENABLE_URMA=ON. Re-run CMake configure with URMA enabled.
```

re-run the CMake configure command above. If CMake was already configured
before this example was added, reconfigure the build directory once.

## Same-Node Test

Start the server:

```bash
./build/output/examples/coro_rpc/coro_rpc_urma_benchmark server \
  --host 127.0.0.1 \
  --port 9001 \
  --device bonding_dev_0
```

Run both latency and throughput tests:

```bash
./build/output/examples/coro_rpc/coro_rpc_urma_benchmark client \
  --host 127.0.0.1 \
  --port 9001 \
  --device bonding_dev_0 \
  --mode both \
  --payload 64 \
  --latency-iters 10000 \
  --connections 64 \
  --duration 10
```

## Cross-Node Test

On the server node:

```bash
./build/output/examples/coro_rpc/coro_rpc_urma_benchmark server \
  --host 0.0.0.0 \
  --port 9001 \
  --device bonding_dev_0
```

On the client node:

```bash
./build/output/examples/coro_rpc/coro_rpc_urma_benchmark client \
  --host <server-ip> \
  --port 9001 \
  --device bonding_dev_0 \
  --mode both \
  --payload 64 \
  --latency-iters 10000 \
  --connections 128 \
  --duration 30
```

Replace `<server-ip>` with the server address reachable from the client.

## Test Modes

Latency only:

```bash
./build/output/examples/coro_rpc/coro_rpc_urma_benchmark client \
  --host <server-ip> \
  --port 9001 \
  --mode latency \
  --payload 64 \
  --latency-iters 10000
```

Throughput only:

```bash
./build/output/examples/coro_rpc/coro_rpc_urma_benchmark client \
  --host <server-ip> \
  --port 9001 \
  --mode throughput \
  --payload 4096 \
  --connections 128 \
  --duration 30
```

## Common Options

```text
--host <ip>              Server listen/connect host. Server default 0.0.0.0,
                         client default 127.0.0.1
--port <port>            Server port. Default 9001
--device <name>          URMA device. Default bonding_dev_0
--eid-index <n>          URMA EID index. Default 0
--payload <bytes>        Echo payload size. Default 64
--buffer-size <bytes>    URMA SEND chunk size. Default 4096 for CTP
--queue-depth <n>        URMA send/recv queue depth. Default 64
--log <trace|debug|info|warn|error> Default info
```

Client-only options:

```text
--mode <latency|throughput|both>
--latency-iters <n>
--warmup-iters <n>
--connections <n>
--concurrency <n> Compatibility option; URMA throughput uses one worker per connection.
--duration <seconds>
--client-threads <n>
```

Server-only option:

```text
--server-threads <n>
```

## Notes

- The default URMA transport type is CTP.
- Server mode defaults to `--host 0.0.0.0`, so cross-node tests work without
  explicitly overriding the listen address. Client mode defaults to
  `127.0.0.1`.
- The default `--buffer-size` is `4096`, matching the documented 4KB max send
  packet size for `bonding_dev_0` CTP.
- The URMA buffer pool allocates one large contiguous memory block and registers
  it as one segment, then splits it into fixed-size buffers. This avoids doing
  one `urma_register_seg` call per 4KB buffer during startup.
- `--connections` controls the number of RPC client connections and throughput
  workers. Each throughput worker owns one `coro_rpc_client` and sends serial
  requests on that connection.
- `--concurrency` is retained as a compatibility option. The URMA benchmark does
  not run multiple throughput coroutines on the same connection because that can
  overrun the current URMA send/recv credit model and produce `WR_FLUSH_ERR`.
- Latency output is in microseconds and includes avg/min/p50/p90/p99/p999/max.
- Throughput output includes request rate and payload MiB/s.
