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

// The basic example about how to start a rpc connection over URMA.
void basic_example() {
  coro_rpc_client client;
  coro_rpc_server server;

  server.init_urma();
  server.register_handler<echo>();
  auto future = server.async_start();
  if (future.hasResult()) {
    ELOG_ERROR << future.result().value().message();
    return;
  }

  // Client connects to server - URMA is auto-initialized from server config
  auto ec = syncAwait(client.connect(std::string{server.address()} + ":" +
                                     std::to_string(server.port())));
  if (ec) {
    ELOG_ERROR << ec.message();
    return;
  }

  std::string data(1024 * 1024 * 10, 'A');
  auto result = syncAwait(client.call<echo>(data));
  if (result != data) {
    ELOG_ERROR << "echo data err";
  }
  else {
    ELOG_INFO << "echo ok!";
  }
  server.stop();
  return;
}

// This example is about how to configure the detail URMA option.
void set_option() {
  /* init global device, should call before any other call */
  coro_io::get_global_urma_device(coro_io::urma_init_config_t{
      .dev_name = "",  /*URMA device name, default is empty, which means choice
                          the first URMA device*/
      .buffer_pool_config =
          {
              .buffer_size = 256 * 1024,             /*buffer size*/
              .max_memory_usage = 20 * 1024 * 1024,  /*max memory usage*/
              .idle_timeout = 5s,
          },
      .eid_index = 0 /*EID index to use*/});

  coro_rpc_client client;
  coro_rpc_client::config conf;
  auto urma_config = coro_io::urma_socket_t::config_t{
      .recv_buffer_cnt = 4,  // buffer cnt of recv queue
      .send_buffer_cnt = 4,   // buffer cnt of send queue
      .buffer_size = 256 * 1024,  // buffer size 256KB
      .device_name = "",  // empty means auto-select
      .eid_index = 0      // EID index
  };
  client.init_urma(urma_config);

  coro_rpc_server server;
  server.init_urma(urma_config);

  server.register_handler<echo>();
  auto future = server.async_start();
  if (future.hasResult()) {
    ELOG_ERROR << future.result().value().message();
    return;
  }

  auto ec = syncAwait(client.connect(std::string{server.address()} + ":" +
                                     std::to_string(server.port())));
  if (ec) {
    ELOG_ERROR << ec.message();
    return;
  }

  std::string data(1024 * 1024 * 10, 'A');
  auto result = syncAwait(client.call<echo>(data));
  if (result != data) {
    ELOG_ERROR << "echo data err";
  }
  else {
    ELOG_INFO << "echo ok!";
  }
  server.stop();
}

int main() {
  easylog::logger<>::instance().set_min_severity(easylog::Severity::INFO);
  set_option();
  basic_example();
  return 0;
}
