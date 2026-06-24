# Mooncake URMA RPC Transport Design

Date: 2026-06-17

## Context

Mooncake uses two different transport layers:

1. **TransferEngine data-plane transports** implemented inside Mooncake. These include RDMA and TCP, and the classic transfer engine already auto-selects RDMA when HCAs are discovered, otherwise TCP.
2. **yalantinglibs `coro_rpc` control-plane RPC transports** used by Mooncake master/client, HA master service, transfer-engine `RpcCommunicator`, and several client/offload helper paths.

This design covers only the second layer: enabling yalantinglibs URMA as a `coro_rpc` transport in Mooncake.

Current Mooncake `coro_rpc` behavior is controlled by `MC_RPC_PROTOCOL`:

- unset or non-`rdma`: use default TCP.
- `rdma`: use yalantinglibs IBV/RDMA.

Current RDMA integration is explicit:

- Server side calls `server.init_ibv()`.
- Client pool side sets `pool_conf.client_config.socket_config = coro_io::ib_socket_t::config_t{}`.

Mooncake currently has no `init_urma()` usage and no `YLT_ENABLE_URMA` compile definition. Mooncake does have `USE_UB`/URMA-related build logic for its own UB transport, but that is separate from yalantinglibs `coro_rpc` URMA.

## Goal

Add a new Mooncake RPC protocol value:

```text
MC_RPC_PROTOCOL=urma
```

with behavior parallel to the existing `MC_RPC_PROTOCOL=rdma` logic.

## Non-goals

- Do not change the meaning of `MC_RPC_PROTOCOL=rdma`; it remains IBV/RDMA.
- Do not introduce `MC_RPC_PROTOCOL=urma_auto`.
- Do not add automatic URMA-to-TCP fallback for this iteration.
- Do not change Mooncake TransferEngine data-plane RDMA/TCP selection.
- Do not replace Mooncake's own RDMA transport with yalantinglibs URMA.

## Desired behavior

```text
MC_RPC_PROTOCOL unset / tcp / other
    -> coro_rpc uses TCP

MC_RPC_PROTOCOL=rdma
    -> coro_rpc uses yalantinglibs IBV/RDMA, preserving current behavior

MC_RPC_PROTOCOL=urma
    -> coro_rpc uses yalantinglibs URMA
```

When `MC_RPC_PROTOCOL=urma` is set but yalantinglibs/Mooncake is not compiled with `YLT_ENABLE_URMA`, the code should either fail at build time for URMA-specific branches or log an explicit runtime error in guarded helper paths. It should not silently treat `urma` as `rdma`.

## yalantinglibs capabilities used

yalantinglibs already provides the required public API when built with `YLT_ENABLE_URMA`:

- `coro_rpc_client::init_urma(const coro_io::urma_socket_t::config_t&)`
- `coro_rpc_server_base::init_urma(const coro_io::urma_socket_t::config_t&)`
- `coro_io::urma_socket_t::config_t`

Client pools can select the socket type by assigning `coro_io::urma_socket_t::config_t{}` into `coro_rpc_client::config::socket_config`, analogous to the existing IBV assignment.

No new yalantinglibs runtime API is required for the selected design.

## Mooncake build changes

Mooncake currently does:

```cmake
find_package(yalantinglibs CONFIG REQUIRED)
add_compile_definitions(YLT_ENABLE_IBV)
```

Add a Mooncake build option for yalantinglibs URMA RPC support, for example:

```cmake
option(USE_YLT_URMA_RPC "Enable yalantinglibs URMA RPC transport" OFF)

if (USE_YLT_URMA_RPC)
  add_compile_definitions(YLT_ENABLE_URMA)
endif()
```

The dependency build path must ensure yalantinglibs itself is configured with URMA enabled, e.g. `-DYLT_ENABLE_URMA=ON`, when `USE_YLT_URMA_RPC` is enabled. Otherwise Mooncake may compile against headers that do not expose URMA support, or link without `-lurma`.

## Mooncake code changes

Add `MC_RPC_PROTOCOL=urma` branches parallel to existing `rdma` branches.

### Server-side branches

Where Mooncake currently does:

```cpp
if (value && std::string_view(value) == "rdma") {
    server.init_ibv();
}
```

extend to:

```cpp
if (value && std::string_view(value) == "rdma") {
    server.init_ibv();
}
#ifdef YLT_ENABLE_URMA
else if (value && std::string_view(value) == "urma") {
    server.init_urma();
}
#endif
```

Primary locations:

- `mooncake-store/src/master.cpp`
- `mooncake-store/src/ha/leadership/master_service_supervisor.cpp`
- `mooncake-store/tests/test_server_helpers.h`
- `mooncake-transfer-engine/src/transport/rpc_communicator/rpc_communicator.cpp`

### Client-pool branches

Where Mooncake currently does:

```cpp
if (value && std::string_view(value) == "rdma") {
    pool_conf.client_config.socket_config = coro_io::ib_socket_t::config_t{};
}
```

extend to:

```cpp
if (value && std::string_view(value) == "rdma") {
    pool_conf.client_config.socket_config = coro_io::ib_socket_t::config_t{};
}
#ifdef YLT_ENABLE_URMA
else if (value && std::string_view(value) == "urma") {
    pool_conf.client_config.socket_config = coro_io::urma_socket_t::config_t{};
}
#endif
```

Primary locations:

- `mooncake-transfer-engine/src/transport/rpc_communicator/rpc_communicator.cpp`
- `mooncake-store/include/master_client.h`
- `mooncake-store/src/real_client.cpp`

## Optional helper refactor

To reduce duplicated environment parsing, Mooncake may add a small helper later:

```cpp
enum class RpcProtocol { Tcp, Rdma, Urma };

RpcProtocol GetRpcProtocolFromEnv();

template <typename SocketConfigVariant>
void ConfigureRpcClientSocket(SocketConfigVariant& socket_config);

template <typename Server>
void ConfigureRpcServer(Server& server);
```

This helper is optional for the initial implementation. The first implementation can be a minimal parallel branch to reduce risk.

## Error handling and logging

- Preserve current `rdma` logging behavior.
- Add logs when `MC_RPC_PROTOCOL=urma` enables URMA on server/client-pool paths.
- If URMA support is not compiled but `MC_RPC_PROTOCOL=urma` is set, log a clear warning or error in paths where that can be checked.
- Do not silently fall back to TCP for `MC_RPC_PROTOCOL=urma` in this iteration, because the user explicitly selected URMA.

## Testing strategy

### Build tests

1. Build Mooncake without `USE_YLT_URMA_RPC` to confirm existing TCP/RDMA behavior is unchanged.
2. Build Mooncake with `USE_YLT_URMA_RPC=ON` and yalantinglibs built with `YLT_ENABLE_URMA=ON`.

### Unit / integration tests without URMA hardware

- Run existing Mooncake RPC tests with `MC_RPC_PROTOCOL` unset.
- Run existing RDMA-related tests only where IBV hardware/environment is available.
- Where possible, run a compile-only test for `MC_RPC_PROTOCOL=urma` branches on a URMA-enabled build.

### URMA hardware validation

On a host with URMA device and liburma available:

1. Start Mooncake master/server with `MC_RPC_PROTOCOL=urma`.
2. Start matching client with `MC_RPC_PROTOCOL=urma`.
3. Confirm logs show URMA transport selected.
4. Run master client operations and transfer-engine `RpcCommunicator` calls that use `coro_rpc`.
5. Confirm existing `MC_RPC_PROTOCOL=rdma` still selects IBV/RDMA, not URMA.

## Open implementation notes

- The first implementation should focus on the paths that already support `MC_RPC_PROTOCOL=rdma`.
- Other `coro_rpc` users that currently do not honor `MC_RPC_PROTOCOL=rdma` can be updated in a later cleanup unless the target deployment needs those paths immediately.
- If Mooncake vendors or installs yalantinglibs via `dependencies.sh`, that script must pass the URMA enable flag when `USE_YLT_URMA_RPC` is desired.
