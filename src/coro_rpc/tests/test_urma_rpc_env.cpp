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

TEST_CASE("urma rpc enabled without usable device keeps client constructible") {
  scoped_env_var enable("URMA_RPC_ENABLE", "1");
  scoped_env_var device("URMA_RPC_DEVICE", "device_that_should_not_exist_for_test");

  coro_rpc::coro_rpc_client client;
  CHECK(std::holds_alternative<coro_rpc::coro_rpc_client::tcp_config>(
      client.get_config().socket_config));
}

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
#else
TEST_CASE("urma rpc env tests compile without urma support") {
  coro_rpc::coro_rpc_client client;
  CHECK(std::holds_alternative<coro_rpc::coro_rpc_client::tcp_config>(
      client.get_config().socket_config));
}
#endif
