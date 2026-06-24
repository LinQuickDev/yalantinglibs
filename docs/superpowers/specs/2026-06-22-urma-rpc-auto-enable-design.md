# URMA RPC Auto Enable Design

Date: 2026-06-22

## Context

Mooncake creates yalantinglibs `coro_rpc` clients and servers with the normal TCP defaults. The current yalantinglibs URMA implementation works similarly to the IBV/RDMA path: callers can explicitly select URMA by calling `coro_rpc_client::init_urma()`, `coro_rpc_server_base::init_urma()`, or by assigning `coro_io::urma_socket_t::config_t` to a client config.

The new requirement is to enable URMA for Mooncake without changing Mooncake code. When Mooncake continues to create normal TCP clients and servers, yalantinglibs should be able to detect an environment opt-in and transparently upgrade the default TCP RPC path to URMA if the local environment has a usable URMA device.

Existing relevant yalantinglibs behavior:

- Client default `coro_rpc_client::config::socket_config` is `tcp_config{}`.
- Explicit IBV and URMA client configs are stored in the same `socket_config` variant.
- Server URMA support is controlled by `coro_rpc_server_base::init_urma()`, which sets an internal `urma_config_`.
- Server accept still begins with a TCP socket. If the client sends the URMA magic byte, the server's existing `update_to_urma()` path wraps the accepted TCP socket with `coro_io::urma_socket_t` and completes the URMA handshake.
- `coro_io::urma_socket_t::config_t` already contains URMA resource parameters such as `cq_size`, buffer counts, `device_name`, `eid_index`, and `tp_type`.

## Goal

Add a yalantinglibs-only automatic URMA selection path for Mooncake RPC:

```text
YLT_ENABLE_URMA compiled + URMA_RPC_ENABLE enabled + default TCP RPC + usable URMA device
    -> use yalantinglibs URMA RPC

Otherwise
    -> keep the existing TCP behavior
```

Mooncake must not need source changes for this feature. Existing Mooncake code that creates default TCP `coro_rpc` clients and servers should become URMA-capable by setting environment variables only.

## Non-goals

- Do not modify Mooncake source code.
- Do not change the meaning of existing explicit `init_ibv()` or `init_urma()` calls.
- Do not replace explicit IBV/RDMA selection with URMA.
- Do not force failure when `URMA_RPC_ENABLE` is set but no URMA device is available; this iteration falls back to TCP.
- Do not make SSL/NTLS socket configs auto-upgrade to URMA.
- Do not add a new Mooncake environment variable such as `MC_RPC_PROTOCOL=urma`.

## Desired behavior

### Default behavior

When `URMA_RPC_ENABLE` is unset or disabled, yalantinglibs keeps the current behavior:

- default client config uses TCP;
- default server accepts normal TCP RPC;
- explicit IBV and explicit URMA continue to work as before.

### Auto-enable behavior

When yalantinglibs is compiled with `YLT_ENABLE_URMA` and `URMA_RPC_ENABLE` is enabled:

1. A default TCP `coro_rpc_client` attempts to build a URMA config from environment variables.
2. A default TCP `coro_rpc_server_base` attempts to set its internal `urma_config_` from the same environment-derived config.
3. If a usable URMA device is found, the client sends URMA magic during connection setup and the server upgrades the accepted TCP socket through the existing URMA path.
4. If no usable URMA device is found or URMA resource initialization fails during probing, yalantinglibs logs the reason and keeps TCP.

### Explicit transport behavior

The automatic path must not override explicit caller choices:

- If a client config already holds `coro_io::ib_socket_t::config_t`, use IBV.
- If a client config already holds `coro_io::urma_socket_t::config_t`, use that URMA config.
- If a server explicitly calls `init_ibv()`, keep IBV behavior.
- If a server explicitly calls `init_urma()`, keep that URMA config.
- If SSL/NTLS is explicitly selected, keep SSL/NTLS behavior.

## Environment variables

### Enable switch

`URMA_RPC_ENABLE` controls automatic URMA selection.

Enabled values:

- `1`
- `ON`, `on`
- `TRUE`, `true`
- `YES`, `yes`

Unset, empty, `0`, `OFF`, `false`, `no`, or any unrecognized value is treated as disabled.

### URMA config variables

The helper builds `coro_io::urma_socket_t::config_t` from these variables. If a variable is unset or invalid, the existing default from `urma_socket_t::config_t` is used.

| Environment variable | Config field | Default |
|---|---|---|
| `URMA_RPC_DEVICE` | `device_name` | empty, auto-select |
| `URMA_RPC_EID_INDEX` | `eid_index` | `0` |
| `URMA_RPC_CQ_SIZE` | `cq_size` | `128` |
| `URMA_RPC_RECV_BUFFER_CNT` | `recv_buffer_cnt` | `8` |
| `URMA_RPC_SEND_BUFFER_CNT` | `send_buffer_cnt` | `4` |
| `URMA_RPC_BUFFER_SIZE` | `buffer_size` | `4 * 1024` |
| `URMA_RPC_MAX_MEMORY_USAGE` | `max_memory_usage` | `256 MiB` |
| `URMA_RPC_TP_TYPE` | `tp_type` | `URMA_CTP` |

`URMA_RPC_TP_TYPE` supports:

- `ctp`, `CTP`, or `0` -> `URMA_CTP`
- `rtp`, `RTP`, or `1` -> `URMA_RTP`

Invalid numeric values are ignored with a warning and the default value is used.

## Proposed implementation

### New helper boundary

Add a small URMA environment helper under the existing URMA RPC implementation area. The helper should only be compiled when `YLT_ENABLE_URMA` is defined.

Suggested public functions in namespace `coro_io` or `coro_io::detail`:

```cpp
bool urma_rpc_env_enabled();
std::optional<coro_io::urma_socket_t::config_t> try_make_urma_rpc_config();
```

Responsibilities:

1. Parse `URMA_RPC_ENABLE`.
2. Parse optional config environment variables.
3. Probe whether the configured URMA device can be initialized through existing `get_global_urma_device(config)` behavior.
4. Return a complete `urma_socket_t::config_t` when URMA is usable.
5. Return `std::nullopt` when auto-enable is disabled or probing fails.

The helper must avoid throwing to callers. It should catch URMA-related exceptions, log a warning, and return `std::nullopt` so default TCP remains available.

### Client integration

In `coro_rpc_client`, apply the helper only when the caller is still using the default TCP socket config.

A safe integration point is during `init_config(conf)` or immediately before visiting `conf.socket_config` to initialize the wrapper:

```cpp
#ifdef YLT_ENABLE_URMA
if (std::holds_alternative<tcp_config>(conf.socket_config)) {
  if (auto urma_config = coro_io::try_make_urma_rpc_config()) {
    conf.socket_config = *urma_config;
  }
}
#endif
```

This preserves all explicit non-TCP socket configs.

### Server integration

In `coro_rpc_server_base`, apply the helper only when the server has not explicitly selected another transport that should control protocol upgrade.

A safe integration point is constructor initialization after explicit config has been processed, or before `async_start()` starts accept loops:

```cpp
#ifdef YLT_ENABLE_URMA
if (!urma_config_.has_value() && !ibv_config_.has_value() && !use_ssl_) {
  if (auto urma_config = coro_io::try_make_urma_rpc_config()) {
    urma_config_ = *urma_config;
  }
}
#endif
```

The actual condition must be guarded so it only references `ibv_config_` and `use_ssl_` when their corresponding compile flags are enabled.

Once `urma_config_` is set, the existing server path handles protocol upgrade:

1. accept TCP socket;
2. run normal RPC handshake;
3. receive URMA magic as a protocol error magic byte;
4. call existing `update_to_urma()`;
5. continue the RPC connection on URMA.

### Device probing and caching

The helper may call `get_global_urma_device()` with the parsed config to probe device availability. Because that path initializes the global URMA device manager and buffer pool, the helper should be called sparingly.

Recommended caching:

- Cache the auto-detection result in a function-local static value or equivalent one-time initialization.
- If the first check succeeds, reuse the returned config for later clients/servers.
- If the first check fails because no device is available, cache the failure for the process lifetime. This avoids repeated `urma_get_device_list` attempts for every client.

This behavior matches environment-driven process startup: users are expected to set URMA environment variables before creating RPC clients and servers.

### Logging

Log only state transitions and user-actionable issues:

- `ELOG_INFO` when automatic URMA is enabled successfully, including device name, eid index, buffer size, and TP type.
- `ELOG_WARN` when `URMA_RPC_ENABLE` is enabled but probing fails and yalantinglibs falls back to TCP.
- `ELOG_WARN` when an environment variable is invalid and the default is used.
- No log, or debug-level only, when `URMA_RPC_ENABLE` is not enabled.

## Compatibility

### Source compatibility

No public Mooncake API changes are required. Existing Mooncake code continues to compile without modification.

### Runtime compatibility

Default runtime behavior is unchanged unless `URMA_RPC_ENABLE` is enabled and URMA support was compiled in.

If `YLT_ENABLE_URMA` is not compiled, the helper and auto-upgrade code are not active. Setting `URMA_RPC_ENABLE` has no effect and default TCP remains unchanged.

### Mixed deployment behavior

For a connection to use URMA, both client and server processes should have:

- yalantinglibs compiled with `YLT_ENABLE_URMA`;
- `URMA_RPC_ENABLE` enabled;
- usable URMA devices and compatible URMA parameters.

If either side does not enable URMA or lacks a usable device, that side remains on TCP. Deployments should enable the variable consistently on Mooncake client and server processes when URMA is desired.

## Error handling

- Invalid environment variable values do not fail startup; defaults are used.
- `URMA_RPC_ENABLE` with no usable URMA device falls back to TCP.
- URMA probing exceptions are caught and logged, then TCP is used.
- Explicit `init_urma()` remains explicit: if a caller directly requests URMA and initialization fails later, existing explicit URMA error behavior remains unchanged.

## Testing strategy

### Build tests

1. Build without `YLT_ENABLE_URMA` and run existing TCP tests.
2. Build with `YLT_ENABLE_URMA` and run existing TCP tests with `URMA_RPC_ENABLE` unset.
3. Build with `YLT_ENABLE_URMA` and run existing TCP tests with `URMA_RPC_ENABLE=1` on a host without URMA hardware; tests should still pass through TCP fallback.

### Environment parsing tests

Add small tests or compile-time test helpers for:

- enabled values: `1`, `ON`, `true`, `yes`;
- disabled or invalid values: unset, `0`, `OFF`, `false`, `abc`;
- invalid numeric values use defaults;
- valid `URMA_RPC_TP_TYPE` values map to the expected URMA TP type.

### URMA hardware validation

On a host with URMA devices and liburma available:

1. Start a default TCP-style `coro_rpc` server with `URMA_RPC_ENABLE=1`.
2. Start a default TCP-style `coro_rpc` client with `URMA_RPC_ENABLE=1`.
3. Confirm logs show automatic URMA enablement.
4. Run RPC calls and confirm they complete over URMA.
5. Unset `URMA_RPC_ENABLE` and confirm the same code path uses TCP.
6. Explicitly select IBV and confirm auto URMA does not override it.
7. Explicitly select URMA and confirm explicit URMA still works.

## Open implementation notes

- The helper should live near URMA socket/device code to avoid spreading environment parsing across `coro_rpc_client` and `coro_rpc_server_base`.
- Keep environment parsing small and header-only, matching the current yalantinglibs header style.
- The first implementation should prefer correctness and compatibility over aggressive runtime re-detection. Process-lifetime caching is acceptable.
- Existing design docs for `MC_RPC_PROTOCOL=urma` are superseded for this requirement because they require Mooncake changes.
