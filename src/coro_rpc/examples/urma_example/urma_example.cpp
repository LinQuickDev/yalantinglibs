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
#include <memory>
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

std::string_view echo(std::string_view data) { return data; }

void check_echo_result(const coro_rpc::rpc_result<std::string_view>& result,
                       std::string_view expected) {
  if (!result) {
    ELOG_ERROR << "echo RPC failed: code=" << result.error().val()
               << ", message=" << result.error().msg;
    return;
  }
  if (result.value() == expected) {
    ELOG_INFO << "echo ok!";
    return;
  }

  auto actual = result.value();
  auto mismatch = std::mismatch(actual.begin(), actual.end(), expected.begin(),
                                expected.end());
  auto mismatch_offset =
      static_cast<std::size_t>(mismatch.first - actual.begin());
  ELOG_ERROR << "echo data err: expected_size=" << expected.size()
             << ", actual_size=" << actual.size()
             << ", first_mismatch_offset=" << mismatch_offset;
}

// The basic example about how to start a rpc connection over URMA.
void basic_example() {
  ELOG_INFO << "basic_example: starting";
  coro_rpc_client client;
  coro_rpc_server server;

  ELOG_INFO << "basic_example: calling server.init_urma";
  server.init_urma();
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

  // Client connects to server - URMA is auto-initialized from server config
  ELOG_INFO << "set_option: server address=" << server.address() << " port=" << server.port();
  auto ec = syncAwait(client.connect(std::string{server.address()} + ":" +
                                     std::to_string(server.port())));
  if (ec) {
    ELOG_ERROR << ec.message();
    return;
  }

  std::string data(1024 * 1024 * 10, 'A');
  auto result = syncAwait(client.call<echo>(data));
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
              .buffer_size = 256 * 1024,             /*buffer size*/
              .max_memory_usage = 20 * 1024 * 1024,  /*max memory usage*/
              .idle_timeout = 5s,
          },
      .eid_index = 0 /*EID index to use*/});

  ELOG_INFO << "set_option: creating client";
  coro_rpc_client client;
  coro_rpc_client::config conf;
  ELOG_INFO << "set_option: client created, configuring urma";
  auto urma_config = coro_io::urma_socket_t::config_t{
      .recv_buffer_cnt = 4,  // buffer cnt of recv queue
      .send_buffer_cnt = 4,   // buffer cnt of send queue
      .buffer_size = 256 * 1024,  // buffer size 256KB
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

  std::string data(1024 * 1024 * 10, 'A');
  auto result = syncAwait(client.call<echo>(data));
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
