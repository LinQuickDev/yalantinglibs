# URMA RPC Auto Enable Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Enable yalantinglibs `coro_rpc` default TCP clients and servers to automatically use URMA when `URMA_RPC_ENABLE` is enabled and a usable URMA device exists, without requiring Mooncake source changes.

**Architecture:** Add one focused URMA environment helper that parses `URMA_RPC_*` variables, probes URMA device availability, and returns an optional `coro_io::urma_socket_t::config_t`. Integrate the helper only into default TCP `coro_rpc_client` and default TCP `coro_rpc_server_base` paths, preserving explicit IBV, URMA, SSL, and NTLS selections. If probing fails, keep TCP.

**Tech Stack:** C++20 header-only yalantinglibs, doctest, CMake, `coro_rpc`, `coro_io::urma_socket_t`, liburma behind `YLT_ENABLE_URMA`.

---

## File structure

### Create

- `include/ylt/coro_io/urma/urma_rpc_env.hpp`
  - Responsibility: parse `URMA_RPC_*` environment variables, build `urma_socket_t::config_t`, probe a usable URMA device, cache auto-detection result, and return `std::optional<urma_socket_t::config_t>`.
  - Compiled only when `YLT_ENABLE_URMA` is defined.

- `src/coro_rpc/tests/test_urma_rpc_env.cpp`
  - Responsibility: doctest coverage for environment parsing and TCP fallback behavior when auto-enable is set but no usable URMA device is available.
  - Contains guarded tests so the test target still builds when `YLT_ENABLE_URMA` is off.

### Modify

- `include/ylt/coro_rpc/impl/coro_rpc_client.hpp`
  - Responsibility: if the caller uses default TCP `socket_config`, apply the optional environment-derived URMA config before initializing the socket wrapper.

- `include/ylt/coro_rpc/impl/coro_rpc_server.hpp`
  - Responsibility: if the server has not selected explicit IBV, URMA, SSL, or NTLS, apply the optional environment-derived URMA config so the existing URMA magic-byte upgrade path can run.

- `src/coro_rpc/tests/CMakeLists.txt`
  - Responsibility: add the new doctest source file to `coro_rpc_test`.

### Existing spec

- `docs/superpowers/specs/2026-06-22-urma-rpc-auto-enable-design.md`

--- 

## Task 1: Add focused environment parsing tests

**Files:**
- Create: `C:\workspace\code\opensource\yalantinglibs\src\coro_rpc\tests\test_urma_rpc_env.cpp`
- Modify: `C:\workspace\code\opensource\yalantinglibs\src\coro_rpc\tests\CMakeLists.txt:3-15`

- [ ] **Step 1: Inspect current test target before editing**

Run:

```bash
git diff -- src/coro_rpc/tests/CMakeLists.txt
```

Expected: no output, or only unrelated pre-existing changes. If there are unrelated local changes, stop and report them before editing.

- [ ] **Step 2: Create the failing environment parsing test**

Create `src/coro_rpc/tests/test_urma_rpc_env.cpp` with this content:

```cpp
/*
 * Copyright (c) 2026, Alibaba Group Holding Limited;
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "doctest.h"

#include <cstdlib>
#include <string>
#include <vector>
#include <variant>

#include <ylt/coro_rpc/coro_rpc_client.hpp>

#ifdef _WIN32
inline void set_test_env(const char* name, const char* value) {
  _putenv_s(name, value);
}
inline void unset_test_env(const char* name) { _putenv_s(name, ""); }
#else
inline void set_test_env(const char* name, const char* value) {
  setenv(name, value, 1);
}
inline void unset_test_env(const char* name) { unsetenv(name); }
#endif

class scoped_env_var {
 public:
  scoped_env_var(const char* name, const char* value) : name_(name) {
    const char* old = std::getenv(name);
    if (old) {
      old_value_ = old;
      had_value_ = true;
    }
    set_test_env(name, value);
  }

  ~scoped_env_var() {
    if (had_value_) {
      set_test_env(name_.c_str(), old_value_.c_str());
    }
    else {
      unset_test_env(name_.c_str());
    }
  }

 private:
  std::string name_;
  std::string old_value_;
  bool had_value_ = false;
};

#ifdef YLT_ENABLE_URMA
#include <ylt/coro_io/urma/urma_rpc_env.hpp>

TEST_CASE("urma rpc env enable parsing") {
  {
    scoped_env_var env("URMA_RPC_ENABLE", "1");
    CHECK(coro_io::detail::urma_rpc_env_enabled());
  }
  {
    scoped_env_var env("URMA_RPC_ENABLE", "ON");
    CHECK(coro_io::detail::urma_rpc_env_enabled());
  }
  {
    scoped_env_var env("URMA_RPC_ENABLE", "true");
    CHECK(coro_io::detail::urma_rpc_env_enabled());
  }
  {
    scoped_env_var env("URMA_RPC_ENABLE", "yes");
    CHECK(coro_io::detail::urma_rpc_env_enabled());
  }
  {
    scoped_env_var env("URMA_RPC_ENABLE", "0");
    CHECK_FALSE(coro_io::detail::urma_rpc_env_enabled());
  }
  {
    scoped_env_var env("URMA_RPC_ENABLE", "abc");
    CHECK_FALSE(coro_io::detail::urma_rpc_env_enabled());
  }
}

TEST_CASE("urma rpc env config parsing uses defaults for invalid values") {
  scoped_env_var enable("URMA_RPC_ENABLE", "1");
  scoped_env_var device("URMA_RPC_DEVICE", "test_dev");
  scoped_env_var eid("URMA_RPC_EID_INDEX", "7");
  scoped_env_var cq("URMA_RPC_CQ_SIZE", "abc");
  scoped_env_var recv("URMA_RPC_RECV_BUFFER_CNT", "9");
  scoped_env_var send("URMA_RPC_SEND_BUFFER_CNT", "10");
  scoped_env_var buffer("URMA_RPC_BUFFER_SIZE", "8192");
  scoped_env_var memory("URMA_RPC_MAX_MEMORY_USAGE", "16777216");
  scoped_env_var tp("URMA_RPC_TP_TYPE", "rtp");

  auto config = coro_io::detail::make_urma_rpc_config_from_env();
  CHECK(config.device_name == "test_dev");
  CHECK(config.eid_index == 7);
  CHECK(config.cq_size == coro_io::urma_socket_t::config_t{}.cq_size);
  CHECK(config.recv_buffer_cnt == 9);
  CHECK(config.send_buffer_cnt == 10);
  CHECK(config.buffer_size == 8192);
  CHECK(config.max_memory_usage == 16777216);
  CHECK(config.tp_type == URMA_RTP);
}

TEST_CASE("urma rpc env disabled keeps default client tcp config") {
  scoped_env_var enable("URMA_RPC_ENABLE", "0");

  coro_rpc::coro_rpc_client client;
  CHECK(std::holds_alternative<coro_rpc::coro_rpc_client::tcp_config>(
      client.get_config().socket_config));
}
#else
TEST_CASE("urma rpc env tests compile without urma support") {
  coro_rpc::coro_rpc_client client;
  CHECK(std::holds_alternative<coro_rpc::coro_rpc_client::tcp_config>(
      client.get_config().socket_config));
}
#endif
```

This test intentionally references `coro_io::detail::urma_rpc_env_enabled` and `coro_io::detail::make_urma_rpc_config_from_env`, which do not exist yet. It should fail to compile until Task 2 adds the helper.

- [ ] **Step 3: Add the test file to `coro_rpc_test`**

In `src/coro_rpc/tests/CMakeLists.txt`, change the `TEST_SRCS` block from:

```cmake
set(TEST_SRCS
        test_acceptor.cpp
        test_coro_rpc_server.cpp
        test_coro_rpc_client.cpp
        test_register_handler.cpp
        test_router.cpp
        test_connection.cpp
        test_function_name.cpp
        test_variadic.cpp
        test_parallel.cpp
        test_client_filter.cpp
        test_abi_compatible.cpp
        )
```

to:

```cmake
set(TEST_SRCS
        test_acceptor.cpp
        test_coro_rpc_server.cpp
        test_coro_rpc_client.cpp
        test_register_handler.cpp
        test_router.cpp
        test_connection.cpp
        test_function_name.cpp
        test_variadic.cpp
        test_parallel.cpp
        test_client_filter.cpp
        test_abi_compatible.cpp
        test_urma_rpc_env.cpp
        )
```

- [ ] **Step 4: Run the focused build to verify the test fails before implementation**

If a build directory already exists, run the target build command used in this repository. A typical command is:

```bash
cmake --build build --target coro_rpc_test -j 4
```

Expected with `YLT_ENABLE_URMA=ON`: compilation fails because `ylt/coro_io/urma/urma_rpc_env.hpp` or the helper functions do not exist yet.

Expected with `YLT_ENABLE_URMA=OFF`: compilation may pass because the URMA-only helper references are excluded; that is acceptable for this pre-implementation check.

- [ ] **Step 5: Commit Task 1 if commits are authorized**

Only commit if the user has explicitly asked for commits in this session. Otherwise skip this step and state it was skipped.

```bash
git add src/coro_rpc/tests/CMakeLists.txt src/coro_rpc/tests/test_urma_rpc_env.cpp
git commit -m "test: add urma rpc env parsing coverage"
```

---

## Task 2: Implement the URMA environment helper

**Files:**
- Create: `C:\workspace\code\opensource\yalantinglibs\include\ylt\coro_io\urma\urma_rpc_env.hpp`
- Test: `C:\workspace\code\opensource\yalantinglibs\src\coro_rpc\tests\test_urma_rpc_env.cpp`

- [ ] **Step 1: Create the helper header**

Create `include/ylt/coro_io/urma/urma_rpc_env.hpp` with this content:

```cpp
/*
 * Copyright (c) 2026, Alibaba Group Holding Limited;
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#ifdef YLT_ENABLE_URMA
#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include "ylt/coro_io/urma/urma_socket.hpp"
#include "ylt/easylog.hpp"

namespace coro_io::detail {

inline std::string urma_rpc_lower_ascii(std::string_view value) {
  std::string result(value);
  std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return result;
}

inline const char* urma_rpc_getenv(const char* name) { return std::getenv(name); }

inline bool urma_rpc_env_enabled() {
  const char* value = urma_rpc_getenv("URMA_RPC_ENABLE");
  if (value == nullptr || *value == '\0') return false;

  auto normalized = urma_rpc_lower_ascii(value);
  return normalized == "1" || normalized == "on" || normalized == "true" ||
         normalized == "yes";
}

template <typename T>
inline bool urma_rpc_parse_integer(std::string_view value, T& output) {
  static_assert(std::is_integral_v<T>);
  T parsed{};
  const auto* first = value.data();
  const auto* last = value.data() + value.size();
  auto [ptr, ec] = std::from_chars(first, last, parsed);
  if (ec != std::errc{} || ptr != last) return false;
  output = parsed;
  return true;
}

template <typename T>
inline void urma_rpc_parse_env_integer(const char* name, T& field) {
  const char* value = urma_rpc_getenv(name);
  if (value == nullptr || *value == '\0') return;

  T parsed{};
  if (!urma_rpc_parse_integer(std::string_view(value), parsed)) {
    ELOG_WARN << "invalid " << name << " value: " << value
              << "; use default " << field;
    return;
  }
  field = parsed;
}

inline void urma_rpc_parse_env_tp_type(urma_tp_type_t& tp_type) {
  const char* value = urma_rpc_getenv("URMA_RPC_TP_TYPE");
  if (value == nullptr || *value == '\0') return;

  auto normalized = urma_rpc_lower_ascii(value);
  if (normalized == "ctp" || normalized == "0") {
    tp_type = URMA_CTP;
    return;
  }
  if (normalized == "rtp" || normalized == "1") {
    tp_type = URMA_RTP;
    return;
  }

  ELOG_WARN << "invalid URMA_RPC_TP_TYPE value: " << value
            << "; use default " << static_cast<int>(tp_type);
}

inline coro_io::urma_socket_t::config_t make_urma_rpc_config_from_env() {
  coro_io::urma_socket_t::config_t config{};

  if (const char* device = urma_rpc_getenv("URMA_RPC_DEVICE");
      device != nullptr) {
    config.device_name = device;
  }

  urma_rpc_parse_env_integer("URMA_RPC_EID_INDEX", config.eid_index);
  urma_rpc_parse_env_integer("URMA_RPC_CQ_SIZE", config.cq_size);
  urma_rpc_parse_env_integer("URMA_RPC_RECV_BUFFER_CNT",
                               config.recv_buffer_cnt);
  urma_rpc_parse_env_integer("URMA_RPC_SEND_BUFFER_CNT",
                               config.send_buffer_cnt);
  urma_rpc_parse_env_integer("URMA_RPC_BUFFER_SIZE", config.buffer_size);
  urma_rpc_parse_env_integer("URMA_RPC_MAX_MEMORY_USAGE",
                               config.max_memory_usage);
  urma_rpc_parse_env_tp_type(config.tp_type);

  return config;
}

inline std::optional<coro_io::urma_socket_t::config_t>
probe_urma_rpc_config(coro_io::urma_socket_t::config_t config) {
  try {
    auto device = coro_io::get_global_urma_device(
        {.dev_name = config.device_name,
         .buffer_pool_config = {.buffer_size = config.buffer_size,
                                .max_memory_usage = config.max_memory_usage},
         .eid_index = config.eid_index});
    if (!device || !device->is_valid() || !device->get_buffer_pool()) {
      ELOG_WARN << "URMA_RPC_ENABLE is enabled but no usable URMA device was "
                   "found; fall back to TCP";
      return std::nullopt;
    }

    config.device_name = device->name();
    config.eid_index = device->eid_index();
    ELOG_INFO << "URMA RPC auto enable succeeded: device="
              << config.device_name << ", eid_index=" << config.eid_index
              << ", tp_type=" << static_cast<int>(config.tp_type)
              << ", cq_size=" << config.cq_size
              << ", recv_buffer_cnt=" << config.recv_buffer_cnt
              << ", send_buffer_cnt=" << config.send_buffer_cnt
              << ", buffer_size=" << config.buffer_size
              << ", max_memory_usage=" << config.max_memory_usage;
    return config;
  } catch (const std::exception& e) {
    ELOG_WARN << "URMA RPC auto enable failed: " << e.what()
              << "; fall back to TCP";
    return std::nullopt;
  }
}

}  // namespace coro_io::detail

namespace coro_io {

inline std::optional<coro_io::urma_socket_t::config_t>
try_make_urma_rpc_config() {
  if (!detail::urma_rpc_env_enabled()) return std::nullopt;

  static const auto cached_config = []() {
    return detail::probe_urma_rpc_config(
        detail::make_urma_rpc_config_from_env());
  }();
  return cached_config;
}

}  // namespace coro_io
#endif
```

- [ ] **Step 2: Build the focused test target**

Run:

```bash
cmake --build build --target coro_rpc_test -j 4
```

Expected: if the existing build directory is valid, `coro_rpc_test` compiles. If the build directory does not exist, configure first with the repository's usual options, then rerun the build.

- [ ] **Step 3: Run the focused doctest cases**

Run:

```bash
build/output/tests/coro_rpc_test --test-case="urma rpc env*"
```

Expected with `YLT_ENABLE_URMA=ON`: the new URMA parsing tests pass.

Expected with `YLT_ENABLE_URMA=OFF`: the fallback compile test passes.

On Windows if the executable path differs, run the generated `coro_rpc_test.exe` from the build output directory with the same `--test-case` argument.

- [ ] **Step 4: Commit Task 2 if commits are authorized**

Only commit if the user has explicitly asked for commits in this session. Otherwise skip this step and state it was skipped.

```bash
git add include/ylt/coro_io/urma/urma_rpc_env.hpp src/coro_rpc/tests/test_urma_rpc_env.cpp
git commit -m "feat: add urma rpc env helper"
```

---

## Task 3: Auto-select URMA for default TCP clients

**Files:**
- Modify: `C:\workspace\code\opensource\yalantinglibs\include\ylt\coro_rpc\impl\coro_rpc_client.hpp:64-66`
- Modify: `C:\workspace\code\opensource\yalantinglibs\include\ylt\coro_rpc\impl\coro_rpc_client.hpp:560-570`
- Test: `C:\workspace\code\opensource\yalantinglibs\src\coro_rpc\tests\test_urma_rpc_env.cpp`

- [ ] **Step 1: Add the helper include**

In `include/ylt/coro_rpc/impl/coro_rpc_client.hpp`, replace this block:

```cpp
#ifdef YLT_ENABLE_URMA
#include "ylt/coro_io/urma/urma_socket.hpp"
#endif
```

with:

```cpp
#ifdef YLT_ENABLE_URMA
#include "ylt/coro_io/urma/urma_rpc_env.hpp"
#include "ylt/coro_io/urma/urma_socket.hpp"
#endif
```

- [ ] **Step 2: Apply auto URMA before socket wrapper initialization**

In `include/ylt/coro_rpc/impl/coro_rpc_client.hpp`, replace `init_config`:

```cpp
  [[nodiscard]] bool init_config(const config &conf) {
    create_tp_ = std::chrono::steady_clock::now();
    config_ = conf;
    control_->socket_wrapper_.set_local_ip(config_.local_ip);
    control_->client_id = conf.client_id;
    return std::visit(
        [this](auto &socket_config) {
          return init_socket_wrapper(socket_config);
        },
        conf.socket_config);
  };
```

with:

```cpp
  [[nodiscard]] bool init_config(const config &conf) {
    create_tp_ = std::chrono::steady_clock::now();
    config_ = conf;
#ifdef YLT_ENABLE_URMA
    if (std::holds_alternative<tcp_config>(config_.socket_config)) {
      if (auto urma_config = coro_io::try_make_urma_rpc_config()) {
        config_.socket_config = *urma_config;
      }
    }
#endif
    control_->socket_wrapper_.set_local_ip(config_.local_ip);
    control_->client_id = config_.client_id;
    return std::visit(
        [this](auto &socket_config) {
          return init_socket_wrapper(socket_config);
        },
        config_.socket_config);
  };
```

This uses `config_` for both the visit and `client_id` after optional auto-upgrade.

- [ ] **Step 3: Build the focused test target**

Run:

```bash
cmake --build build --target coro_rpc_test -j 4
```

Expected: `coro_rpc_test` compiles.

- [ ] **Step 4: Run the focused URMA tests**

Run:

```bash
build/output/tests/coro_rpc_test --test-case="urma rpc env*"
```

Expected: all URMA tests pass. The disabled-env client test confirms default client config remains TCP when auto-enable is off.

- [ ] **Step 5: Commit Task 3 if commits are authorized**

Only commit if the user has explicitly asked for commits in this session. Otherwise skip this step and state it was skipped.

```bash
git add include/ylt/coro_rpc/impl/coro_rpc_client.hpp src/coro_rpc/tests/test_urma_rpc_env.cpp
git commit -m "feat: auto select urma for default rpc clients"
```

---

## Task 4: Auto-select URMA for default TCP servers

**Files:**
- Modify: `C:\workspace\code\opensource\yalantinglibs\include\ylt\coro_rpc\impl\coro_rpc_server.hpp`
- Test: `C:\workspace\code\opensource\yalantinglibs\src\coro_rpc\tests\test_urma_rpc_env.cpp`

- [ ] **Step 1: Add the helper include**

In `include/ylt/coro_rpc/impl/coro_rpc_server.hpp`, after:

```cpp
#include "ylt/coro_io/io_context_pool.hpp"
#include "ylt/coro_io/server_acceptor.hpp"
```

add:

```cpp
#ifdef YLT_ENABLE_URMA
#include "ylt/coro_io/urma/urma_rpc_env.hpp"
#endif
```

- [ ] **Step 2: Add the server auto-init helper method**

In `include/ylt/coro_rpc/impl/coro_rpc_server.hpp`, after `init_urma`:

```cpp
#ifdef YLT_ENABLE_URMA
  void init_urma(const coro_io::urma_socket_t::config_t &conf = {}) {
    urma_config_ = conf;
  }
#endif
```

add this method:

```cpp
#ifdef YLT_ENABLE_URMA
  void init_urma_from_env_if_default() {
    if (urma_config_.has_value()) return;
#ifdef YLT_ENABLE_IBV
    if (ibv_config_.has_value()) return;
#endif
#ifdef YLT_ENABLE_SSL
    if (use_ssl_) return;
#endif
    if (auto urma_config = coro_io::try_make_urma_rpc_config()) {
      urma_config_ = *urma_config;
    }
  }
#endif
```

This method is guarded so it does not reference `ibv_config_` or `use_ssl_` unless those members exist.

- [ ] **Step 3: Call the helper from the simple port constructor**

In the constructor that currently ends with:

```cpp
    acceptors_.push_back(
        std::make_unique<coro_io::tcp_server_acceptor>(address, port));
  }
```

change it to:

```cpp
    acceptors_.push_back(
        std::make_unique<coro_io::tcp_server_acceptor>(address, port));
#ifdef YLT_ENABLE_URMA
    init_urma_from_env_if_default();
#endif
  }
```

- [ ] **Step 4: Call the helper from the address constructor**

In the constructor that currently ends with:

```cpp
    acceptors_.push_back(
        std::make_unique<coro_io::tcp_server_acceptor>(address));
  }
```

change it to:

```cpp
    acceptors_.push_back(
        std::make_unique<coro_io::tcp_server_acceptor>(address));
#ifdef YLT_ENABLE_URMA
    init_urma_from_env_if_default();
#endif
  }
```

- [ ] **Step 5: Call the helper from the config constructor after explicit URMA processing**

In the config constructor, find this block near the end:

```cpp
#ifdef YLT_ENABLE_URMA
    if (config.urma_config) {
      init_urma(config.urma_config.value());
    }
#endif
  }
```

replace it with:

```cpp
#ifdef YLT_ENABLE_URMA
    if (config.urma_config) {
      init_urma(config.urma_config.value());
    }
    init_urma_from_env_if_default();
#endif
  }
```

Because `init_urma_from_env_if_default()` returns immediately when `urma_config_` already has a value, explicit URMA configs are preserved.

- [ ] **Step 6: Add a server fallback test for no-URMA environments**

In `src/coro_rpc/tests/test_urma_rpc_env.cpp`, inside the `#ifdef YLT_ENABLE_URMA` section after the existing tests, add:

```cpp
TEST_CASE("urma rpc enabled without usable device keeps client constructible") {
  scoped_env_var enable("URMA_RPC_ENABLE", "1");
  scoped_env_var device("URMA_RPC_DEVICE", "device_that_should_not_exist_for_test");

  coro_rpc::coro_rpc_client client;
  CHECK(std::holds_alternative<coro_rpc::coro_rpc_client::tcp_config>(
      client.get_config().socket_config));
}
```

This verifies the required fallback behavior for the client side when probing fails. Server fallback is covered by compilation and existing TCP tests because the server helper also returns without setting URMA when probing fails.

- [ ] **Step 7: Build the focused test target**

Run:

```bash
cmake --build build --target coro_rpc_test -j 4
```

Expected: `coro_rpc_test` compiles.

- [ ] **Step 8: Run the focused URMA tests**

Run:

```bash
build/output/tests/coro_rpc_test --test-case="urma rpc env*"
```

Expected: all URMA tests pass. On hosts without the named URMA device, the enabled-with-missing-device test confirms TCP fallback.

- [ ] **Step 9: Commit Task 4 if commits are authorized**

Only commit if the user has explicitly asked for commits in this session. Otherwise skip this step and state it was skipped.

```bash
git add include/ylt/coro_rpc/impl/coro_rpc_server.hpp src/coro_rpc/tests/test_urma_rpc_env.cpp
git commit -m "feat: auto select urma for default rpc servers"
```

---

## Task 5: Verify explicit transport choices are preserved

**Files:**
- Modify: `C:\workspace\code\opensource\yalantinglibs\src\coro_rpc\tests\test_urma_rpc_env.cpp`

- [ ] **Step 1: Add explicit transport preservation tests**

In `src/coro_rpc/tests/test_urma_rpc_env.cpp`, inside the `#ifdef YLT_ENABLE_URMA` section after the existing tests, add:

```cpp
TEST_CASE("urma rpc env does not override explicit client urma config") {
  scoped_env_var enable("URMA_RPC_ENABLE", "0");

  coro_rpc::coro_rpc_client::config config;
  coro_io::urma_socket_t::config_t explicit_config{};
  explicit_config.device_name = "explicit_device";
  explicit_config.eid_index = 5;
  config.socket_config = explicit_config;

  coro_rpc::coro_rpc_client client(coro_io::get_global_executor(), config);
  REQUIRE(std::holds_alternative<coro_io::urma_socket_t::config_t>(
      client.get_config().socket_config));
  const auto& stored = std::get<coro_io::urma_socket_t::config_t>(
      client.get_config().socket_config);
  CHECK(stored.device_name == "explicit_device");
  CHECK(stored.eid_index == 5);
}

#ifdef YLT_ENABLE_IBV
TEST_CASE("urma rpc env does not override explicit client ibv config") {
  scoped_env_var enable("URMA_RPC_ENABLE", "1");

  coro_rpc::coro_rpc_client::config config;
  config.socket_config = coro_io::ib_socket_t::config_t{};

  coro_rpc::coro_rpc_client client(coro_io::get_global_executor(), config);
  CHECK(std::holds_alternative<coro_io::ib_socket_t::config_t>(
      client.get_config().socket_config));
}
#endif
```

These tests only inspect stored config. The explicit URMA client may fail wrapper initialization if the explicit device does not exist, but `init_config` must still preserve the explicit variant in `client.get_config()`.

- [ ] **Step 2: Build the focused test target**

Run:

```bash
cmake --build build --target coro_rpc_test -j 4
```

Expected: `coro_rpc_test` compiles.

- [ ] **Step 3: Run the focused URMA tests**

Run:

```bash
build/output/tests/coro_rpc_test --test-case="urma rpc env*"
```

Expected: all URMA tests pass.

- [ ] **Step 4: Commit Task 5 if commits are authorized**

Only commit if the user has explicitly asked for commits in this session. Otherwise skip this step and state it was skipped.

```bash
git add src/coro_rpc/tests/test_urma_rpc_env.cpp
git commit -m "test: cover explicit rpc transport preservation"
```

---

## Task 6: Run broader verification and inspect diffs

**Files:**
- Verify all files changed by Tasks 1-5.

- [ ] **Step 1: Inspect the complete diff**

Run:

```bash
git diff -- include/ylt/coro_io/urma/urma_rpc_env.hpp include/ylt/coro_rpc/impl/coro_rpc_client.hpp include/ylt/coro_rpc/impl/coro_rpc_server.hpp src/coro_rpc/tests/CMakeLists.txt src/coro_rpc/tests/test_urma_rpc_env.cpp
```

Expected: the diff only contains the URMA environment helper, the guarded client/server integration, and tests.

- [ ] **Step 2: Build the main coro_rpc test target**

Run:

```bash
cmake --build build --target coro_rpc_test -j 4
```

Expected: target builds successfully.

- [ ] **Step 3: Run all coro_rpc tests**

Run:

```bash
build/output/tests/coro_rpc_test
```

Expected: all doctest cases in `coro_rpc_test` pass.

- [ ] **Step 4: Run CTest for the coro_rpc test if available**

Run:

```bash
ctest --test-dir build -R coro_rpc_test --output-on-failure
```

Expected: `coro_rpc_test` passes.

- [ ] **Step 5: Verify no Mooncake files changed**

Run:

```bash
git diff --name-only
```

Expected: changed files are limited to yalantinglibs source/tests/docs. No files under a Mooncake repository should appear.

- [ ] **Step 6: Commit verification changes if commits are authorized**

Only commit if the user has explicitly asked for commits in this session. Otherwise skip this step and state it was skipped.

```bash
git add include/ylt/coro_io/urma/urma_rpc_env.hpp include/ylt/coro_rpc/impl/coro_rpc_client.hpp include/ylt/coro_rpc/impl/coro_rpc_server.hpp src/coro_rpc/tests/CMakeLists.txt src/coro_rpc/tests/test_urma_rpc_env.cpp docs/superpowers/specs/2026-06-22-urma-rpc-auto-enable-design.md docs/superpowers/plans/2026-06-22-urma-rpc-auto-enable.md
git commit -m "feat: auto enable urma rpc from environment"
```

---

## Manual URMA hardware validation

Run these only on a host with URMA devices, liburma, and a yalantinglibs build configured with `YLT_ENABLE_URMA=ON`.

- [ ] **Step 1: Run default TCP-style RPC with auto URMA enabled**

Set:

```bash
export URMA_RPC_ENABLE=1
```

Start a default TCP-style `coro_rpc` server and client, such as an existing simple example or Mooncake process that does not explicitly call `init_urma()`.

Expected: logs include `URMA RPC auto enable succeeded` on both sides, and RPC calls complete.

- [ ] **Step 2: Run default TCP-style RPC with auto URMA disabled**

Set:

```bash
unset URMA_RPC_ENABLE
```

Start the same server and client.

Expected: no auto URMA success log is printed, and RPC calls complete over the normal TCP path.

- [ ] **Step 3: Run with explicit transport selections**

Run code paths that explicitly use `init_ibv()` or `init_urma()`.

Expected: explicit IBV remains IBV, explicit URMA remains URMA, and the auto helper does not override either explicit selection.

---

## Self-review checklist

- Spec coverage:
  - Environment switch `URMA_RPC_ENABLE`: Task 2.
  - URMA config environment variables: Task 2.
  - Client default TCP auto-upgrade: Task 3.
  - Server default TCP auto-upgrade: Task 4.
  - Fallback to TCP when no usable device exists: Tasks 2 and 4 tests.
  - Explicit transport preservation: Task 5.
  - Mooncake source remains untouched: Task 6.
- Placeholder scan: no placeholder tasks are intentionally left for implementers; every code-editing step includes exact code.
- Type consistency: helper names are `coro_io::detail::urma_rpc_env_enabled`, `coro_io::detail::make_urma_rpc_config_from_env`, and `coro_io::try_make_urma_rpc_config`; integration tasks use the same names.
