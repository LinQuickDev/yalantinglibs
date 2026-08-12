/*
 * Copyright (c) 2025, Alibaba Group Holding Limited;
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
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#include "async_simple/coro/SyncAwait.h"
#include "ylt/coro_io/client_pool.hpp"
#include "ylt/coro_io/io_context_pool.hpp"
#include "ylt/coro_io/urma/urma_device.hpp"
#include "ylt/coro_io/urma/urma_socket.hpp"
#include "ylt/coro_rpc/coro_rpc_client.hpp"
#include "ylt/coro_rpc/coro_rpc_server.hpp"
#include "ylt/coro_rpc/impl/coro_rpc_client.hpp"
#include "ylt/easylog.hpp"
#include "ylt/easylog/record.hpp"

using namespace coro_rpc;
using namespace async_simple::coro;
using namespace std::chrono_literals;

void set_process_env(const char* name, const std::string& value) {
#ifdef _WIN32
  _putenv_s(name, value.c_str());
#else
  setenv(name, value.c_str(), 1);
#endif
}

void configure_urma_rpc_auto_env() {
  set_process_env("URMA_RPC_ENABLE", "1");
  set_process_env("URMA_RPC_DEVICE", "bonding_dev_0");
  set_process_env("URMA_RPC_EID_INDEX", "0");
  set_process_env("URMA_RPC_RECV_BUFFER_CNT", "64");
  set_process_env("URMA_RPC_SEND_BUFFER_CNT", "64");
  set_process_env("URMA_RPC_BUFFER_SIZE", std::to_string(4 * 1024));
  set_process_env("URMA_RPC_MAX_MEMORY_USAGE", std::to_string(20 * 1024 * 1024));
  set_process_env("URMA_RPC_TP_TYPE", "ctp");
}

std::string_view echo(std::string_view data) { return data; }

bool check_echo_result(const coro_rpc::rpc_result<std::string_view>& result,
                       std::string_view expected) {
  if (!result) {
    ELOG_ERROR << "echo RPC failed: code=" << result.error().val()
               << ", message=" << result.error().msg;
    return false;
  }
  if (result.value() == expected) {
    ELOG_INFO << "echo ok!";
    return true;
  }

  auto actual = result.value();
  auto mismatch = std::mismatch(actual.begin(), actual.end(), expected.begin(),
                                expected.end());
  auto mismatch_offset =
      static_cast<std::size_t>(mismatch.first - actual.begin());
  ELOG_ERROR << "echo data err: expected_size=" << expected.size()
             << ", actual_size=" << actual.size()
             << ", first_mismatch_offset=" << mismatch_offset;
  return false;
}

bool warmup_urma_rpc(coro_rpc_client& client) {
  std::string warmup = "urma warmup";
  ELOG_INFO << "running URMA warmup RPC";
  auto result = syncAwait(client.call_for<echo>(30s, warmup));
  return check_echo_result(result, warmup);
}

// The basic example about how to start a rpc connection over URMA.
void basic_example() {
  ELOG_INFO << "basic_example: starting";
  configure_urma_rpc_auto_env();
  coro_rpc_client client;
  coro_rpc_server server;

  ELOG_INFO << "set_option: registering handler";
  server.register_handler<echo>();
  ELOG_INFO << "set_option: handler registered";
  ELOG_INFO << "set_option: calling server.async_start";
  auto future = server.async_start();
  ELOG_INFO << "set_option: server.async_start returned";
  if (future.hasResult()) {
    ELOG_ERROR << future.result().value().message();
    return;
  }

  // Client and server keep default TCP config; URMA_RPC_* enables the URMA upgrade.
  ELOG_INFO << "set_option: server address=" << server.address() << " port=" << server.port();
  auto ec = syncAwait(client.connect(std::string{server.address()} + ":" +
                                     std::to_string(server.port())));
  if (ec) {
    ELOG_ERROR << ec.message();
    return;
  }

  if (!warmup_urma_rpc(client)) {
    server.stop();
    return;
  }

  std::string data(1024 * 1024 * 10, 'A');
  auto result = syncAwait(client.call_for<echo>(120s, data));
  check_echo_result(result, data);
  server.stop();
  return;
}

// This example is about how to configure the detail URMA option.
void set_option() {
  ELOG_INFO << "set_option: starting";
  /* init global device, should call before any other call */
  ELOG_INFO << "set_option: initializing global urma device";
  coro_io::get_global_urma_device(coro_io::urma_init_config_t{
      .dev_name = "bonding_dev_0",  /*URMA device name, default is empty, which means choice
                          the first URMA device*/
      .buffer_pool_config =
          {
              .buffer_size = 4 * 1024,               /*CTP send packet size*/
              .max_memory_usage = 20 * 1024 * 1024,  /*max memory usage*/
              .idle_timeout = 5s,
          },
      .eid_index = 0 /*EID index to use*/});

  ELOG_INFO << "set_option: creating client";
  coro_rpc_client client;
  coro_rpc_client::config conf;
  ELOG_INFO << "set_option: client created, configuring urma";
  auto urma_config = coro_io::urma_socket_t::config_t{
      .recv_buffer_cnt = 64,  // buffer cnt of recv queue
      .send_buffer_cnt = 64,  // buffer cnt of send queue
      .buffer_size = 4 * 1024,  // CTP max send packet size on bonding_dev_0
      .device_name = "bonding_dev_0",  // empty means auto-select
      .eid_index = 0      // EID index
  };
  ELOG_INFO << "set_option: calling client.init_urma";
  if (!client.init_urma(urma_config)) {
    ELOG_ERROR << "URMA client init failed";
    return;
  }
  ELOG_INFO << "set_option: client.init_urma succeeded";

  ELOG_INFO << "set_option: creating server";
  coro_rpc_server server;
  ELOG_INFO << "set_option: calling server.init_urma";
  server.init_urma(urma_config);
  ELOG_INFO << "set_option: server.init_urma done";

  ELOG_INFO << "set_option: registering handler";
  server.register_handler<echo>();
  ELOG_INFO << "set_option: handler registered";
  ELOG_INFO << "set_option: calling server.async_start";
  auto future = server.async_start();
  ELOG_INFO << "set_option: server.async_start returned";
  if (future.hasResult()) {
    ELOG_ERROR << future.result().value().message();
    return;
  }

  ELOG_INFO << "set_option: server address=" << server.address() << " port=" << server.port();
  auto ec = syncAwait(client.connect(std::string{server.address()} + ":" +
                                     std::to_string(server.port())));
  if (ec) {
    ELOG_ERROR << ec.message();
    return;
  }

  if (!warmup_urma_rpc(client)) {
    server.stop();
    return;
  }

  std::string data(1024 * 1024 * 10, 'A');
  auto result = syncAwait(client.call_for<echo>(120s, data));
  check_echo_result(result, data);
  server.stop();
}

int main() {
  easylog::logger<>::instance().set_min_severity(easylog::Severity::DEBUG);
  easylog::logger<>::instance().set_async(false);
  ELOG_INFO << "URMA example main started";
  set_option();
  ELOG_INFO << "set_option completed, now basic_example";
  basic_example();
  ELOG_INFO << "basic_example completed";
  return 0;
}
