# Mooncake URMA RPC Transport Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `MC_RPC_PROTOCOL=urma` to Mooncake's yalantinglibs `coro_rpc` control-plane paths, parallel to existing `MC_RPC_PROTOCOL=rdma` behavior.

**Architecture:** Mooncake keeps TCP as the default and keeps `MC_RPC_PROTOCOL=rdma` mapped to yalantinglibs IBV/RDMA. A new `MC_RPC_PROTOCOL=urma` branch selects `coro_io::urma_socket_t::config_t{}` for client pools and calls `server.init_urma()` for RPC servers, guarded by `YLT_ENABLE_URMA`. Build configuration adds an opt-in `USE_YLT_URMA_RPC` option that defines `YLT_ENABLE_URMA` and configures yalantinglibs with URMA support in `dependencies.sh`.

**Tech Stack:** C++20, CMake, Bash, yalantinglibs `coro_rpc`, yalantinglibs `coro_io::urma_socket_t`, Mooncake `MC_RPC_PROTOCOL` environment variable.

---

## File structure

This plan modifies the Mooncake repository at `C:\workspace\code\opensource\Mooncake`.

### Build files

- Modify `C:\workspace\code\opensource\Mooncake\mooncake-common\common.cmake`
  - Responsibility: global Mooncake CMake options and compile definitions.
  - Add `USE_YLT_URMA_RPC` and define `YLT_ENABLE_URMA` when enabled.

- Modify `C:\workspace\code\opensource\Mooncake\dependencies.sh`
  - Responsibility: dependency installation, including building/installing yalantinglibs submodule.
  - Pass `-DYLT_ENABLE_URMA=ON` to yalantinglibs when requested by environment variable `USE_YLT_URMA_RPC=ON` or `USE_YLT_URMA_RPC=1`.

### RPC call sites

- Modify `C:\workspace\code\opensource\Mooncake\mooncake-transfer-engine\src\transport\rpc_communicator\rpc_communicator.cpp`
  - Responsibility: TransferEngine `coro_rpc` communicator server/client pool.
  - Add URMA include and `MC_RPC_PROTOCOL=urma` client/server branches.

- Modify `C:\workspace\code\opensource\Mooncake\mooncake-store\include\master_client.h`
  - Responsibility: Master client `coro_rpc` client-pool configuration.
  - Add URMA include and helper branch to set `urma_socket_t::config_t{}`.

- Modify `C:\workspace\code\opensource\Mooncake\mooncake-store\src\master.cpp`
  - Responsibility: standalone master RPC server startup.
  - Add `MC_RPC_PROTOCOL=urma` branch calling `server.init_urma()`.

- Modify `C:\workspace\code\opensource\Mooncake\mooncake-store\src\ha\leadership\master_service_supervisor.cpp`
  - Responsibility: HA master RPC server startup.
  - Add `MC_RPC_PROTOCOL=urma` branch calling `server.init_urma()`.

- Modify `C:\workspace\code\opensource\Mooncake\mooncake-store\tests\test_server_helpers.h`
  - Responsibility: test in-process master RPC server helper.
  - Add `MC_RPC_PROTOCOL=urma` branch calling `server_->init_urma()`.

- Modify `C:\workspace\code\opensource\Mooncake\mooncake-store\src\real_client.cpp`
  - Responsibility: `ClientRequester` offload RPC client-pool configuration.
  - Add URMA client-pool branch.

### Plan/spec files already created in yalantinglibs

- Existing spec: `C:\workspace\code\opensource\yalantinglibs\docs\superpowers\specs\2026-06-17-mooncake-urma-rpc-design.md`
- This plan: `C:\workspace\code\opensource\yalantinglibs\docs\superpowers\plans\2026-06-17-mooncake-urma-rpc.md`

---

## Task 1: Add Mooncake build option for yalantinglibs URMA RPC

**Files:**
- Modify: `C:\workspace\code\opensource\Mooncake\mooncake-common\common.cmake:74-95`
- Modify: `C:\workspace\code\opensource\Mooncake\mooncake-common\common.cmake:429-434`

- [ ] **Step 1: Inspect the current CMake option block**

Run:

```bash
git -C C:\workspace\code\opensource\Mooncake diff -- mooncake-common/common.cmake
```

Expected: either no output or unrelated pre-existing local changes. If there are unrelated local changes, stop and report them before editing.

- [ ] **Step 2: Add the option near existing transport options**

In `mooncake-common/common.cmake`, near the existing options:

```cmake
option(USE_TCP "option for using TCP transport" ON)
option(USE_BAREX "option for using accl-barex transport" OFF)
option(USE_ASCEND "option for using npu with HCCL" OFF)
option(USE_ASCEND_DIRECT "option for using ascend npu with adxl engine" OFF)
option(USE_UBSHMEM "option for using ascend npu with shmem" OFF)
option(USE_ASCEND_HETEROGENEOUS "option for transferring between ascend npu and gpu" OFF)
option(USE_MNNVL "option for using Multi-Node NVLink transport" OFF)
option(USE_CXL "option for using CXL protocol" OFF)
option(USE_EFA "option for using AWS EFA transport" OFF)
option(USE_UB "option for using UB protocol transport" OFF)
option(USE_SUNRISE "option for enabling gpu features for Sunrise GPU with Tang runtime" OFF)
```

add:

```cmake
option(USE_YLT_URMA_RPC "option for enabling yalantinglibs URMA RPC transport" OFF)
```

The resulting block should include:

```cmake
option(USE_EFA "option for using AWS EFA transport" OFF)
option(USE_UB "option for using UB protocol transport" OFF)
option(USE_YLT_URMA_RPC "option for enabling yalantinglibs URMA RPC transport" OFF)
option(USE_SUNRISE "option for enabling gpu features for Sunrise GPU with Tang runtime" OFF)
```

- [ ] **Step 3: Define `YLT_ENABLE_URMA` when the option is enabled**

In `mooncake-common/common.cmake`, after:

```cmake
find_package(yalantinglibs CONFIG REQUIRED)
add_compile_definitions(YLT_ENABLE_IBV)
```

change it to:

```cmake
find_package(yalantinglibs CONFIG REQUIRED)
add_compile_definitions(YLT_ENABLE_IBV)
if (USE_YLT_URMA_RPC)
  add_compile_definitions(YLT_ENABLE_URMA)
endif()
```

- [ ] **Step 4: Verify the diff**

Run:

```bash
git -C C:\workspace\code\opensource\Mooncake diff -- mooncake-common/common.cmake
```

Expected diff includes exactly:

```diff
+option(USE_YLT_URMA_RPC "option for enabling yalantinglibs URMA RPC transport" OFF)
...
 add_compile_definitions(YLT_ENABLE_IBV)
+if (USE_YLT_URMA_RPC)
+  add_compile_definitions(YLT_ENABLE_URMA)
+endif()
```

- [ ] **Step 5: Commit Task 1**

Only commit if the user has explicitly asked for commits. Otherwise skip this step and state it was skipped.

```bash
git -C C:\workspace\code\opensource\Mooncake add mooncake-common/common.cmake
git -C C:\workspace\code\opensource\Mooncake commit -m "build: add ylt urma rpc option"
```

---

## Task 2: Pass URMA enable flag when building yalantinglibs dependency

**Files:**
- Modify: `C:\workspace\code\opensource\Mooncake\dependencies.sh:229-241`

- [ ] **Step 1: Inspect current dependency script diff**

Run:

```bash
git -C C:\workspace\code\opensource\Mooncake diff -- dependencies.sh
```

Expected: either no output or unrelated pre-existing local changes. If there are unrelated local changes, stop and report them before editing.

- [ ] **Step 2: Replace the yalantinglibs configure command with option-aware code**

Current code:

```bash
echo "Configuring yalantinglibs..."
cmake .. -DBUILD_EXAMPLES=OFF -DBUILD_BENCHMARK=OFF -DBUILD_UNIT_TESTS=OFF
check_success "Failed to configure yalantinglibs"
```

Replace it with:

```bash
echo "Configuring yalantinglibs..."
ylt_cmake_args=(
    -DBUILD_EXAMPLES=OFF
    -DBUILD_BENCHMARK=OFF
    -DBUILD_UNIT_TESTS=OFF
)

if [ "${USE_YLT_URMA_RPC:-OFF}" = "ON" ] || [ "${USE_YLT_URMA_RPC:-OFF}" = "1" ]; then
    ylt_cmake_args+=( -DYLT_ENABLE_URMA=ON )
fi

cmake .. "${ylt_cmake_args[@]}"
check_success "Failed to configure yalantinglibs"
```

This keeps the default dependency build unchanged and enables yalantinglibs URMA only when the script environment opts in.

- [ ] **Step 3: Verify Bash syntax around the edited section**

Run:

```bash
git -C C:\workspace\code\opensource\Mooncake diff -- dependencies.sh
```

Expected diff includes the new `ylt_cmake_args` array and the `USE_YLT_URMA_RPC` conditional.

- [ ] **Step 4: Run shell syntax check if `bash` is available**

Run:

```bash
bash -n C:\workspace\code\opensource\Mooncake\dependencies.sh
```

Expected: no output and exit code 0.

If `bash` is not available in the environment, skip this step and record that syntax check was skipped because `bash` is unavailable.

- [ ] **Step 5: Commit Task 2**

Only commit if the user has explicitly asked for commits. Otherwise skip this step and state it was skipped.

```bash
git -C C:\workspace\code\opensource\Mooncake add dependencies.sh
git -C C:\workspace\code\opensource\Mooncake commit -m "build: enable ylt urma in dependency build"
```

---

## Task 3: Add URMA support to TransferEngine RpcCommunicator

**Files:**
- Modify: `C:\workspace\code\opensource\Mooncake\mooncake-transfer-engine\src\transport\rpc_communicator\rpc_communicator.cpp:1-14`
- Modify: `C:\workspace\code\opensource\Mooncake\mooncake-transfer-engine\src\transport\rpc_communicator\rpc_communicator.cpp:47-100`

- [ ] **Step 1: Inspect current file diff**

Run:

```bash
git -C C:\workspace\code\opensource\Mooncake diff -- mooncake-transfer-engine/src/transport/rpc_communicator/rpc_communicator.cpp
```

Expected: either no output or unrelated pre-existing local changes. If there are unrelated local changes, stop and report them before editing.

- [ ] **Step 2: Add conditional URMA include**

After:

```cpp
#include <ylt/coro_io/coro_io.hpp>
```

add:

```cpp
#ifdef YLT_ENABLE_URMA
#include <ylt/coro_io/urma/urma_socket.hpp>
#endif
```

- [ ] **Step 3: Extend client pool protocol selection**

Current code:

```cpp
const char* value = std::getenv("MC_RPC_PROTOCOL");
if (value && std::string_view(value) == "rdma") {
    pool_conf.client_config.socket_config =
        coro_io::ib_socket_t::config_t{};
}
client_pools_ =
    std::make_shared<coro_io::client_pools<coro_rpc::coro_rpc_client>>(
        pool_conf);
```

Replace with:

```cpp
const char* value = std::getenv("MC_RPC_PROTOCOL");
if (value && std::string_view(value) == "rdma") {
    pool_conf.client_config.socket_config =
        coro_io::ib_socket_t::config_t{};
}
#ifdef YLT_ENABLE_URMA
else if (value && std::string_view(value) == "urma") {
    pool_conf.client_config.socket_config =
        coro_io::urma_socket_t::config_t{};
}
#endif
client_pools_ =
    std::make_shared<coro_io::client_pools<coro_rpc::coro_rpc_client>>(
        pool_conf);
```

- [ ] **Step 4: Extend server protocol initialization**

Current code:

```cpp
if (value && std::string_view(value) == "rdma") {
    if (server_) {
        try {
            server_->init_ibv();
            LOG(INFO) << "RDMA initialized successfully";
        } catch (const std::exception& e) {
            LOG(ERROR) << "RDMA initialization failed: " << e.what();
            LOG(WARNING) << "Falling back to TCP mode";
            // Continue without RDMA - the server will use TCP
        } catch (...) {
            LOG(ERROR)
                << "RDMA initialization failed with unknown error";
            LOG(WARNING) << "Falling back to TCP mode";
            // Continue without RDMA - the server will use TCP
        }
    } else {
        LOG(ERROR) << "Server pointer is null, cannot initialize RDMA";
        LOG(WARNING) << "Falling back to TCP mode";
    }
}
```

Replace with:

```cpp
if (value && std::string_view(value) == "rdma") {
    if (server_) {
        try {
            server_->init_ibv();
            LOG(INFO) << "RDMA initialized successfully";
        } catch (const std::exception& e) {
            LOG(ERROR) << "RDMA initialization failed: " << e.what();
            LOG(WARNING) << "Falling back to TCP mode";
            // Continue without RDMA - the server will use TCP
        } catch (...) {
            LOG(ERROR)
                << "RDMA initialization failed with unknown error";
            LOG(WARNING) << "Falling back to TCP mode";
            // Continue without RDMA - the server will use TCP
        }
    } else {
        LOG(ERROR) << "Server pointer is null, cannot initialize RDMA";
        LOG(WARNING) << "Falling back to TCP mode";
    }
}
#ifdef YLT_ENABLE_URMA
else if (value && std::string_view(value) == "urma") {
    if (server_) {
        try {
            server_->init_urma();
            LOG(INFO) << "URMA initialized successfully";
        } catch (const std::exception& e) {
            LOG(ERROR) << "URMA initialization failed: " << e.what();
            LOG(WARNING) << "Falling back to TCP mode";
        } catch (...) {
            LOG(ERROR)
                << "URMA initialization failed with unknown error";
            LOG(WARNING) << "Falling back to TCP mode";
        }
    } else {
        LOG(ERROR) << "Server pointer is null, cannot initialize URMA";
        LOG(WARNING) << "Falling back to TCP mode";
    }
}
#endif
```

This mirrors the existing RDMA fallback behavior in this one file. Other Mooncake paths that do not currently catch RDMA initialization exceptions will remain consistent with their existing style.

- [ ] **Step 5: Extend protocol logging**

Current code near the end of initialization:

```cpp
if (value && std::string_view(value) == "rdma") {
    LOG(INFO) << "Using RDMA transport for RPC communication";
} else {
```

Replace with:

```cpp
if (value && std::string_view(value) == "rdma") {
    LOG(INFO) << "Using RDMA transport for RPC communication";
}
#ifdef YLT_ENABLE_URMA
else if (value && std::string_view(value) == "urma") {
    LOG(INFO) << "Using URMA transport for RPC communication";
}
#endif
else {
```

- [ ] **Step 6: Verify diff**

Run:

```bash
git -C C:\workspace\code\opensource\Mooncake diff -- mooncake-transfer-engine/src/transport/rpc_communicator/rpc_communicator.cpp
```

Expected: URMA include, client-pool branch, server branch, and log branch are present; existing RDMA behavior remains unchanged.

- [ ] **Step 7: Commit Task 3**

Only commit if the user has explicitly asked for commits. Otherwise skip this step and state it was skipped.

```bash
git -C C:\workspace\code\opensource\Mooncake add mooncake-transfer-engine/src/transport/rpc_communicator/rpc_communicator.cpp
git -C C:\workspace\code\opensource\Mooncake commit -m "feat: support urma rpc communicator protocol"
```

---

## Task 4: Add URMA client-pool config to MasterClient

**Files:**
- Modify: `C:\workspace\code\opensource\Mooncake\mooncake-store\include\master_client.h:1-14`
- Modify: `C:\workspace\code\opensource\Mooncake\mooncake-store\include\master_client.h:27-46`
- Modify: `C:\workspace\code\opensource\Mooncake\mooncake-store\include\master_client.h:68-72`

- [ ] **Step 1: Inspect current file diff**

Run:

```bash
git -C C:\workspace\code\opensource\Mooncake diff -- mooncake-store/include/master_client.h
```

Expected: either no output or unrelated pre-existing local changes. If there are unrelated local changes, stop and report them before editing.

- [ ] **Step 2: Add conditional URMA include**

After:

```cpp
#include <ylt/coro_io/ibverbs/ib_socket.hpp>
```

add:

```cpp
#ifdef YLT_ENABLE_URMA
#include <ylt/coro_io/urma/urma_socket.hpp>
#endif
```

- [ ] **Step 3: Add URMA helper next to RDMA helper**

After the existing helper:

```cpp
template <typename SocketConfigVariant>
inline void MaybeEnableRdmaSocketConfig(SocketConfigVariant& socket_config) {
    if constexpr (variant_contains_v<SocketConfigVariant,
                                     coro_io::ib_socket_t::config_t>) {
        socket_config = coro_io::ib_socket_t::config_t{};
    }
}
```

add:

```cpp
#ifdef YLT_ENABLE_URMA
template <typename SocketConfigVariant>
inline void MaybeEnableUrmaSocketConfig(SocketConfigVariant& socket_config) {
    if constexpr (variant_contains_v<SocketConfigVariant,
                                     coro_io::urma_socket_t::config_t>) {
        socket_config = coro_io::urma_socket_t::config_t{};
    }
}
#endif
```

- [ ] **Step 4: Extend constructor protocol branch**

Current code:

```cpp
const char* value = std::getenv("MC_RPC_PROTOCOL");
if (value && std::string_view(value) == "rdma") {
    detail::MaybeEnableRdmaSocketConfig(
        pool_conf.client_config.socket_config);
}
client_pools_ =
    std::make_shared<coro_io::client_pools<coro_rpc::coro_rpc_client>>(
        pool_conf);
```

Replace with:

```cpp
const char* value = std::getenv("MC_RPC_PROTOCOL");
if (value && std::string_view(value) == "rdma") {
    detail::MaybeEnableRdmaSocketConfig(
        pool_conf.client_config.socket_config);
}
#ifdef YLT_ENABLE_URMA
else if (value && std::string_view(value) == "urma") {
    detail::MaybeEnableUrmaSocketConfig(
        pool_conf.client_config.socket_config);
}
#endif
client_pools_ =
    std::make_shared<coro_io::client_pools<coro_rpc::coro_rpc_client>>(
        pool_conf);
```

- [ ] **Step 5: Verify diff**

Run:

```bash
git -C C:\workspace\code\opensource\Mooncake diff -- mooncake-store/include/master_client.h
```

Expected: URMA include, `MaybeEnableUrmaSocketConfig`, and constructor branch are present.

- [ ] **Step 6: Commit Task 4**

Only commit if the user has explicitly asked for commits. Otherwise skip this step and state it was skipped.

```bash
git -C C:\workspace\code\opensource\Mooncake add mooncake-store/include/master_client.h
git -C C:\workspace\code\opensource\Mooncake commit -m "feat: support urma master client rpc"
```

---

## Task 5: Add URMA server initialization to master server paths

**Files:**
- Modify: `C:\workspace\code\opensource\Mooncake\mooncake-store\src\master.cpp:1169-1177`
- Modify: `C:\workspace\code\opensource\Mooncake\mooncake-store\src\ha\leadership\master_service_supervisor.cpp:343-350`
- Modify: `C:\workspace\code\opensource\Mooncake\mooncake-store\tests\test_server_helpers.h:88-95`

- [ ] **Step 1: Inspect current diffs**

Run:

```bash
git -C C:\workspace\code\opensource\Mooncake diff -- mooncake-store/src/master.cpp mooncake-store/src/ha/leadership/master_service_supervisor.cpp mooncake-store/tests/test_server_helpers.h
```

Expected: either no output or unrelated pre-existing local changes. If there are unrelated local changes, stop and report them before editing.

- [ ] **Step 2: Extend standalone master server branch**

In `mooncake-store/src/master.cpp`, replace:

```cpp
const char* value = std::getenv("MC_RPC_PROTOCOL");
if (value && std::string_view(value) == "rdma") {
    server.init_ibv();
}
```

with:

```cpp
const char* value = std::getenv("MC_RPC_PROTOCOL");
if (value && std::string_view(value) == "rdma") {
    server.init_ibv();
}
#ifdef YLT_ENABLE_URMA
else if (value && std::string_view(value) == "urma") {
    server.init_urma();
}
#endif
```

- [ ] **Step 3: Extend HA master server branch**

In `mooncake-store/src/ha/leadership/master_service_supervisor.cpp`, replace:

```cpp
const char* protocol = std::getenv("MC_RPC_PROTOCOL");
if (protocol && std::string_view(protocol) == "rdma") {
    server.init_ibv();
}
```

with:

```cpp
const char* protocol = std::getenv("MC_RPC_PROTOCOL");
if (protocol && std::string_view(protocol) == "rdma") {
    server.init_ibv();
}
#ifdef YLT_ENABLE_URMA
else if (protocol && std::string_view(protocol) == "urma") {
    server.init_urma();
}
#endif
```

- [ ] **Step 4: Extend test in-process master server branch**

In `mooncake-store/tests/test_server_helpers.h`, replace:

```cpp
const char* value = std::getenv("MC_RPC_PROTOCOL");
if (value && std::string_view(value) == "rdma") {
    server_->init_ibv();
}
```

with:

```cpp
const char* value = std::getenv("MC_RPC_PROTOCOL");
if (value && std::string_view(value) == "rdma") {
    server_->init_ibv();
}
#ifdef YLT_ENABLE_URMA
else if (value && std::string_view(value) == "urma") {
    server_->init_urma();
}
#endif
```

- [ ] **Step 5: Verify diff**

Run:

```bash
git -C C:\workspace\code\opensource\Mooncake diff -- mooncake-store/src/master.cpp mooncake-store/src/ha/leadership/master_service_supervisor.cpp mooncake-store/tests/test_server_helpers.h
```

Expected: three server paths now have `MC_RPC_PROTOCOL=urma` branches guarded by `YLT_ENABLE_URMA`.

- [ ] **Step 6: Commit Task 5**

Only commit if the user has explicitly asked for commits. Otherwise skip this step and state it was skipped.

```bash
git -C C:\workspace\code\opensource\Mooncake add mooncake-store/src/master.cpp mooncake-store/src/ha/leadership/master_service_supervisor.cpp mooncake-store/tests/test_server_helpers.h
git -C C:\workspace\code\opensource\Mooncake commit -m "feat: support urma master rpc servers"
```

---

## Task 6: Add URMA client-pool config to ClientRequester

**Files:**
- Modify: `C:\workspace\code\opensource\Mooncake\mooncake-store\src\real_client.cpp:5682-5688`

- [ ] **Step 1: Inspect current file diff**

Run:

```bash
git -C C:\workspace\code\opensource\Mooncake diff -- mooncake-store/src/real_client.cpp
```

Expected: either no output or unrelated pre-existing local changes. If there are unrelated local changes, stop and report them before editing.

- [ ] **Step 2: Verify URMA type visibility**

Check whether `real_client.cpp` or one of its included headers already includes yalantinglibs socket headers. If `coro_io::urma_socket_t` is not visible after compilation, add this near the other includes in `real_client.cpp`:

```cpp
#ifdef YLT_ENABLE_URMA
#include <ylt/coro_io/urma/urma_socket.hpp>
#endif
```

Do not add the include if the file already compiles because `real_client.h` includes `coro_rpc` client headers and exposes the URMA type under `YLT_ENABLE_URMA`.

- [ ] **Step 3: Extend `ClientRequester` protocol branch**

Current code:

```cpp
const char *value = std::getenv("MC_RPC_PROTOCOL");
if (value && std::string_view(value) == "rdma") {
    pool_conf.client_config.socket_config =
        coro_io::ib_socket_t::config_t{};
}
```

Replace with:

```cpp
const char *value = std::getenv("MC_RPC_PROTOCOL");
if (value && std::string_view(value) == "rdma") {
    pool_conf.client_config.socket_config =
        coro_io::ib_socket_t::config_t{};
}
#ifdef YLT_ENABLE_URMA
else if (value && std::string_view(value) == "urma") {
    pool_conf.client_config.socket_config =
        coro_io::urma_socket_t::config_t{};
}
#endif
```

- [ ] **Step 4: Verify diff**

Run:

```bash
git -C C:\workspace\code\opensource\Mooncake diff -- mooncake-store/src/real_client.cpp
```

Expected: `ClientRequester` now supports `MC_RPC_PROTOCOL=urma`; optional include appears only if needed.

- [ ] **Step 5: Commit Task 6**

Only commit if the user has explicitly asked for commits. Otherwise skip this step and state it was skipped.

```bash
git -C C:\workspace\code\opensource\Mooncake add mooncake-store/src/real_client.cpp
git -C C:\workspace\code\opensource\Mooncake commit -m "feat: support urma offload rpc client"
```

---

## Task 7: Add documentation for `MC_RPC_PROTOCOL=urma`

**Files:**
- Modify: `C:\workspace\code\opensource\Mooncake\docs\source\getting_started\supported-protocols.md`

- [ ] **Step 1: Inspect current docs diff**

Run:

```bash
git -C C:\workspace\code\opensource\Mooncake diff -- docs/source/getting_started/supported-protocols.md
```

Expected: either no output or unrelated pre-existing local changes. If there are unrelated local changes, stop and report them before editing.

- [ ] **Step 2: Find the RPC protocol/env section**

Search in the file for `MC_RPC_PROTOCOL`. If no section exists, use the consolidated environment examples section near the existing `MOONCAKE_PROTOCOL` examples.

Run:

```bash
git -C C:\workspace\code\opensource\Mooncake grep -n "MC_RPC_PROTOCOL\|MOONCAKE_PROTOCOL" -- docs/source/getting_started/supported-protocols.md
```

Expected: existing protocol environment examples are printed.

- [ ] **Step 3: Add URMA RPC note**

Add this paragraph near the existing protocol environment examples:

```markdown
### yalantinglibs RPC transport

Mooncake's data-plane transport is configured separately from the yalantinglibs `coro_rpc` control-plane transport. The control-plane RPC transport is selected with `MC_RPC_PROTOCOL`:

```bash
# Default: TCP control-plane RPC
unset MC_RPC_PROTOCOL

# Existing IBV/RDMA control-plane RPC
export MC_RPC_PROTOCOL=rdma

# URMA control-plane RPC, requires Mooncake built with USE_YLT_URMA_RPC=ON
# and yalantinglibs built with YLT_ENABLE_URMA=ON.
export MC_RPC_PROTOCOL=urma
```
```

If the file already has an `MC_RPC_PROTOCOL` section, merge this content into that section instead of duplicating headings.

- [ ] **Step 4: Verify docs diff**

Run:

```bash
git -C C:\workspace\code\opensource\Mooncake diff -- docs/source/getting_started/supported-protocols.md
```

Expected: docs clearly state that `MC_RPC_PROTOCOL=urma` is for `coro_rpc` control-plane URMA and requires `USE_YLT_URMA_RPC=ON` plus yalantinglibs `YLT_ENABLE_URMA=ON`.

- [ ] **Step 5: Commit Task 7**

Only commit if the user has explicitly asked for commits. Otherwise skip this step and state it was skipped.

```bash
git -C C:\workspace\code\opensource\Mooncake add docs/source/getting_started/supported-protocols.md
git -C C:\workspace\code\opensource\Mooncake commit -m "docs: document urma rpc protocol"
```

---

## Task 8: Verification

**Files:**
- No source edits in this task.

- [ ] **Step 1: Show final Mooncake diff**

Run:

```bash
git -C C:\workspace\code\opensource\Mooncake diff --stat
```

Expected: modified files include:

```text
dependencies.sh
mooncake-common/common.cmake
mooncake-transfer-engine/src/transport/rpc_communicator/rpc_communicator.cpp
mooncake-store/include/master_client.h
mooncake-store/src/master.cpp
mooncake-store/src/ha/leadership/master_service_supervisor.cpp
mooncake-store/tests/test_server_helpers.h
mooncake-store/src/real_client.cpp
docs/source/getting_started/supported-protocols.md
```

- [ ] **Step 2: Verify no accidental `rdma` semantic changes**

Run:

```bash
git -C C:\workspace\code\opensource\Mooncake diff | findstr /n /c:"MC_RPC_PROTOCOL" /c:"rdma" /c:"urma" /c:"init_ibv" /c:"init_urma"
```

Expected: every new `urma` branch is an `else if` after the existing `rdma` branch; existing `rdma` checks still call `init_ibv()` or set `ib_socket_t::config_t{}`.

- [ ] **Step 3: Verify code compiles in non-URMA configuration**

If CMake is available, run from a suitable build environment:

```bash
cmake -S C:\workspace\code\opensource\Mooncake -B C:\workspace\code\opensource\Mooncake\build-no-urma -DUSE_YLT_URMA_RPC=OFF
cmake --build C:\workspace\code\opensource\Mooncake\build-no-urma --target transfer_engine -j
```

Expected: configure and build complete. `YLT_ENABLE_URMA` branches are not compiled.

If CMake is unavailable in the current shell, record:

```text
Skipped non-URMA CMake verification: cmake is not available in PATH.
```

- [ ] **Step 4: Verify code compiles in URMA configuration**

On a machine with liburma and yalantinglibs URMA support available, run:

```bash
cmake -S C:\workspace\code\opensource\Mooncake -B C:\workspace\code\opensource\Mooncake\build-urma -DUSE_YLT_URMA_RPC=ON
cmake --build C:\workspace\code\opensource\Mooncake\build-urma --target transfer_engine -j
```

Expected: configure and build complete; `coro_io::urma_socket_t::config_t{}` and `server.init_urma()` compile.

If URMA build dependencies are unavailable, record:

```text
Skipped URMA CMake verification: liburma / yalantinglibs URMA build is unavailable in this environment.
```

- [ ] **Step 5: Run available unit/integration tests**

If a Mooncake test build is already available, run the tests that exercise master client/server and `RpcCommunicator`. Example commands must be adjusted to the actual build directory:

```bash
ctest --test-dir C:\workspace\code\opensource\Mooncake\build-no-urma --output-on-failure
```

Expected: existing tests pass in non-URMA mode.

If no build directory exists or CTest is unavailable, record the skip reason exactly.

- [ ] **Step 6: Runtime smoke test on URMA hardware**

On a host with URMA hardware/liburma:

```bash
set MC_RPC_PROTOCOL=urma
# Start the relevant Mooncake master/server binary.
# Start the matching Mooncake client or integration test.
```

Expected:

```text
Logs show URMA RPC selected.
RPC requests complete successfully.
MC_RPC_PROTOCOL=rdma still selects IBV/RDMA in a separate run.
```

- [ ] **Step 7: Final status report**

Report:

- Files changed.
- Whether commits were made or skipped.
- Which verification commands passed.
- Which verification commands were skipped and why.
- Any build/test failures with exact output.

---

## Self-review checklist

- Spec coverage:
  - `MC_RPC_PROTOCOL=urma` is implemented for server and client-pool paths that already support `rdma`.
  - `MC_RPC_PROTOCOL=rdma` remains IBV/RDMA.
  - No `urma_auto` behavior is introduced.
  - Build-side `YLT_ENABLE_URMA` is controlled by an opt-in Mooncake option.
  - Dependency build path can enable yalantinglibs URMA.
  - Docs explain data-plane vs control-plane protocol distinction.

- Placeholder scan:
  - The plan contains no implementation placeholders such as TBD/TODO.
  - Optional later helper refactor is not required for the implementation.

- Type consistency:
  - Server API: `server.init_ibv()` and `server.init_urma()`.
  - Client config API: `coro_io::ib_socket_t::config_t{}` and `coro_io::urma_socket_t::config_t{}`.
  - Compile guard: `YLT_ENABLE_URMA`.
  - Mooncake option: `USE_YLT_URMA_RPC`.
