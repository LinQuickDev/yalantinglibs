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
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "asio/ip/address.hpp"
#include "async_simple/Promise.h"
#include "async_simple/coro/Collect.h"
#include "async_simple/coro/Lazy.h"
#include "async_simple/coro/SyncAwait.h"
#include "ylt/coro_io/io_context_pool.hpp"
#include "ylt/coro_io/urma/urma_device.hpp"
#include "ylt/coro_io/urma/urma_socket.hpp"
#include "ylt/coro_rpc/coro_rpc_client.hpp"
#include "ylt/coro_rpc/coro_rpc_server.hpp"
#include "ylt/easylog.hpp"
#include "ylt/easylog/record.hpp"

using namespace async_simple::coro;
using namespace coro_rpc;
using namespace std::chrono_literals;

std::string_view bench_echo(std::string_view data) { return data; }

struct options_t {
  std::string role = "client";
  std::string mode = "both";
  std::string host = "127.0.0.1";
  uint16_t port = 9001;
  std::string device = "bonding_dev_0";
  int eid_index = 0;
  uint32_t payload_size = 64;
  uint32_t latency_iters = 10000;
  uint32_t warmup_iters = 1000;
  uint32_t concurrency = 64;
  uint32_t connections = 1;
  uint32_t duration_seconds = 10;
  uint32_t server_threads = std::max(1u, std::thread::hardware_concurrency());
  uint32_t client_threads = std::max(1u, std::thread::hardware_concurrency());
  uint16_t queue_depth = 64;
  uint32_t buffer_size = 4 * 1024;
  uint64_t max_memory_usage = 256ull * 1024 * 1024;
  easylog::Severity log_level = easylog::Severity::WARN;
};

void print_usage(const char* program) {
  std::cout
      << "Usage:\n"
      << "  " << program << " server [options]\n"
      << "  " << program << " client [options]\n\n"
      << "Common options:\n"
      << "  --host <ip>              Server listen/connect host. Default 127.0.0.1\n"
      << "  --port <port>            Server port. Default 9001\n"
      << "  --device <name>          URMA device. Default bonding_dev_0\n"
      << "  --eid-index <n>          URMA EID index. Default 0\n"
      << "  --payload <bytes>        Echo payload size. Default 64\n"
      << "  --buffer-size <bytes>    URMA SEND chunk size. Default 4096 for CTP\n"
      << "  --queue-depth <n>        URMA send/recv queue depth. Default 64\n"
      << "  --log <trace|debug|info|warn|error> Default warn\n\n"
      << "Client options:\n"
      << "  --mode <latency|throughput|both> Default both\n"
      << "  --latency-iters <n>      Low-load serial requests. Default 10000\n"
      << "  --warmup-iters <n>       Warmup requests per client. Default 1000\n"
      << "  --concurrency <n>        Throughput coroutines. Default 64\n"
      << "  --connections <n>        URMA RPC connections. Default 1\n"
      << "  --duration <seconds>     Throughput duration. Default 10\n"
      << "  --client-threads <n>     Client executor threads. Default hardware\n\n"
      << "Server options:\n"
      << "  --server-threads <n>     Server threads. Default hardware\n\n"
      << "Examples:\n"
      << "  same node server: " << program
      << " server --host 127.0.0.1 --port 9001\n"
      << "  same node client: " << program
      << " client --host 127.0.0.1 --port 9001 --mode both\n"
      << "  cross node server: " << program
      << " server --host 0.0.0.0 --port 9001\n"
      << "  cross node client: " << program
      << " client --host <server-ip> --port 9001 --mode throughput\n";
}

uint64_t parse_u64(std::string_view value, std::string_view name) {
  char* end = nullptr;
  auto str = std::string(value);
  auto result = std::strtoull(str.c_str(), &end, 10);
  if (end == str.c_str() || *end != '\0') {
    throw std::invalid_argument("invalid numeric option: " + std::string(name));
  }
  return result;
}

easylog::Severity parse_log_level(std::string_view value) {
  if (value == "trace") return easylog::Severity::TRACE;
  if (value == "debug") return easylog::Severity::DEBUG;
  if (value == "info") return easylog::Severity::INFO;
  if (value == "warn") return easylog::Severity::WARN;
  if (value == "error") return easylog::Severity::ERROR;
  throw std::invalid_argument("invalid --log value");
}

options_t parse_options(int argc, char** argv) {
  options_t opt;
  if (argc >= 2) opt.role = argv[1];
  if (opt.role != "server" && opt.role != "client") {
    print_usage(argv[0]);
    throw std::invalid_argument("role must be server or client");
  }

  for (int i = 2; i < argc; ++i) {
    std::string_view key = argv[i];
    auto require_value = [&]() -> std::string_view {
      if (i + 1 >= argc) {
        throw std::invalid_argument("missing value for " + std::string(key));
      }
      return argv[++i];
    };

    if (key == "--help" || key == "-h") {
      print_usage(argv[0]);
      std::exit(0);
    }
    else if (key == "--host") {
      opt.host = require_value();
    }
    else if (key == "--port") {
      opt.port = static_cast<uint16_t>(parse_u64(require_value(), key));
    }
    else if (key == "--device") {
      opt.device = require_value();
    }
    else if (key == "--eid-index") {
      opt.eid_index = static_cast<int>(parse_u64(require_value(), key));
    }
    else if (key == "--payload") {
      opt.payload_size = static_cast<uint32_t>(parse_u64(require_value(), key));
    }
    else if (key == "--buffer-size") {
      opt.buffer_size = static_cast<uint32_t>(parse_u64(require_value(), key));
    }
    else if (key == "--queue-depth") {
      opt.queue_depth = static_cast<uint16_t>(parse_u64(require_value(), key));
    }
    else if (key == "--mode") {
      opt.mode = require_value();
    }
    else if (key == "--latency-iters") {
      opt.latency_iters =
          static_cast<uint32_t>(parse_u64(require_value(), key));
    }
    else if (key == "--warmup-iters") {
      opt.warmup_iters =
          static_cast<uint32_t>(parse_u64(require_value(), key));
    }
    else if (key == "--concurrency") {
      opt.concurrency = static_cast<uint32_t>(parse_u64(require_value(), key));
    }
    else if (key == "--connections") {
      opt.connections = static_cast<uint32_t>(parse_u64(require_value(), key));
    }
    else if (key == "--duration") {
      opt.duration_seconds =
          static_cast<uint32_t>(parse_u64(require_value(), key));
    }
    else if (key == "--server-threads") {
      opt.server_threads =
          static_cast<uint32_t>(parse_u64(require_value(), key));
    }
    else if (key == "--client-threads") {
      opt.client_threads =
          static_cast<uint32_t>(parse_u64(require_value(), key));
    }
    else if (key == "--log") {
      opt.log_level = parse_log_level(require_value());
    }
    else {
      throw std::invalid_argument("unknown option: " + std::string(key));
    }
  }

  if (opt.mode != "latency" && opt.mode != "throughput" &&
      opt.mode != "both") {
    throw std::invalid_argument("--mode must be latency, throughput, or both");
  }
  opt.concurrency = std::max<uint32_t>(opt.concurrency, 1);
  opt.connections = std::max<uint32_t>(opt.connections, 1);
  opt.queue_depth = std::max<uint16_t>(opt.queue_depth, 1);
  return opt;
}

coro_io::urma_socket_t::config_t make_urma_config(const options_t& opt) {
  return coro_io::urma_socket_t::config_t{
      .cq_size = static_cast<uint32_t>(opt.queue_depth * 2 + 8),
      .recv_buffer_cnt = opt.queue_depth,
      .send_buffer_cnt = opt.queue_depth,
      .buffer_size = opt.buffer_size,
      .device_name = opt.device,
      .eid_index = opt.eid_index,
      .tp_type = URMA_CTP};
}

void init_global_urma(const options_t& opt) {
  coro_io::get_global_urma_device(coro_io::urma_init_config_t{
      .dev_name = opt.device,
      .buffer_pool_config =
          {
              .buffer_size = opt.buffer_size,
              .max_memory_usage = opt.max_memory_usage,
              .idle_timeout = 5s,
          },
      .eid_index = opt.eid_index});
}

Lazy<bool> connect_client(coro_rpc_client& client, const options_t& opt) {
  if (!client.init_urma(make_urma_config(opt))) {
    ELOG_ERROR << "init URMA client failed";
    co_return false;
  }
  auto ec = co_await client.connect(opt.host, std::to_string(opt.port), 30s);
  if (ec) {
    ELOG_ERROR << "connect failed: " << ec.message();
    co_return false;
  }
  co_return true;
}

Lazy<bool> warmup(coro_rpc_client& client, const std::string& payload,
                  uint32_t count) {
  for (uint32_t i = 0; i < count; ++i) {
    auto result = co_await client.call_for<bench_echo>(30s, payload);
    if (!result || result.value().size() != payload.size()) {
      ELOG_ERROR << "warmup failed at iteration " << i;
      co_return false;
    }
  }
  co_return true;
}

void print_latency_result(std::vector<uint64_t>& samples) {
  if (samples.empty()) {
    std::cout << "latency: no samples\n";
    return;
  }
  std::sort(samples.begin(), samples.end());
  auto percentile = [&](double p) {
    auto index = static_cast<std::size_t>((samples.size() - 1) * p);
    return samples[index];
  };
  auto sum = std::accumulate(samples.begin(), samples.end(), uint64_t{0});
  auto avg = static_cast<double>(sum) / static_cast<double>(samples.size());
  std::cout << std::fixed << std::setprecision(2)
            << "latency_us count=" << samples.size() << " avg=" << avg
            << " min=" << samples.front() << " p50=" << percentile(0.50)
            << " p90=" << percentile(0.90) << " p99=" << percentile(0.99)
            << " p999=" << percentile(0.999) << " max=" << samples.back()
            << "\n";
}

Lazy<void> run_latency(const options_t& opt, const std::string& payload) {
  coro_rpc_client client;
  if (!(co_await connect_client(client, opt))) co_return;
  if (!(co_await warmup(client, payload, opt.warmup_iters))) co_return;

  std::vector<uint64_t> samples;
  samples.reserve(opt.latency_iters);
  for (uint32_t i = 0; i < opt.latency_iters; ++i) {
    auto begin = std::chrono::steady_clock::now();
    auto result = co_await client.call_for<bench_echo>(30s, payload);
    auto end = std::chrono::steady_clock::now();
    if (!result || result.value().size() != payload.size()) {
      ELOG_ERROR << "latency call failed at iteration " << i;
      break;
    }
    samples.push_back(static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
            .count()));
  }
  print_latency_result(samples);
}

struct worker_result_t {
  uint64_t requests = 0;
  uint64_t errors = 0;
  uint64_t bytes = 0;
};

Lazy<worker_result_t> throughput_worker(coro_rpc_client& client,
                                         const std::string& payload,
                                         std::chrono::steady_clock::time_point
                                             deadline) {
  worker_result_t stat;
  while (std::chrono::steady_clock::now() < deadline) {
    auto result = co_await client.call_for<bench_echo>(30s, payload);
    if (!result || result.value().size() != payload.size()) {
      stat.errors++;
      continue;
    }
    stat.requests++;
    stat.bytes += payload.size();
  }
  co_return stat;
}

Lazy<void> run_throughput(const options_t& opt, const std::string& payload) {
  std::vector<std::unique_ptr<coro_rpc_client>> clients;
  clients.reserve(opt.connections);
  for (uint32_t i = 0; i < opt.connections; ++i) {
    auto client = std::make_unique<coro_rpc_client>();
    if (!(co_await connect_client(*client, opt))) co_return;
    if (!(co_await warmup(*client, payload, opt.warmup_iters))) co_return;
    clients.push_back(std::move(client));
  }

  auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(opt.duration_seconds);
  std::vector<Lazy<worker_result_t>> workers;
  workers.reserve(opt.concurrency);
  for (uint32_t i = 0; i < opt.concurrency; ++i) {
    workers.push_back(throughput_worker(*clients[i % clients.size()], payload,
                                        deadline));
  }

  auto begin = std::chrono::steady_clock::now();
  auto results = co_await collectAll(std::move(workers));
  auto end = std::chrono::steady_clock::now();

  worker_result_t total;
  for (auto& item : results) {
    auto result = item.value();
    total.requests += result.requests;
    total.errors += result.errors;
    total.bytes += result.bytes;
  }

  auto seconds =
      std::chrono::duration_cast<std::chrono::duration<double>>(end - begin)
          .count();
  auto rps = static_cast<double>(total.requests) / seconds;
  auto mbps = static_cast<double>(total.bytes) / seconds / 1024.0 / 1024.0;
  std::cout << std::fixed << std::setprecision(2)
            << "throughput duration_s=" << seconds
            << " requests=" << total.requests << " errors=" << total.errors
            << " rps=" << rps << " payload_mib_per_s=" << mbps
            << " concurrency=" << opt.concurrency
            << " connections=" << opt.connections << "\n";
}

int run_server(const options_t& opt) {
  init_global_urma(opt);
  coro_rpc_server server(opt.server_threads, opt.port, opt.host);
  server.init_urma(make_urma_config(opt));
  server.register_handler<bench_echo>();
  std::cout << "URMA RPC benchmark server listening on " << opt.host << ":"
            << opt.port << ", device=" << opt.device
            << ", eid_index=" << opt.eid_index
            << ", buffer_size=" << opt.buffer_size
            << ", queue_depth=" << opt.queue_depth << "\n";
  return !server.start();
}

int run_client(const options_t& opt) {
  coro_io::get_global_executor(opt.client_threads);
  init_global_urma(opt);
  std::string payload(opt.payload_size, 'x');
  std::cout << "URMA RPC benchmark client target " << opt.host << ":"
            << opt.port << ", mode=" << opt.mode
            << ", payload=" << opt.payload_size
            << ", buffer_size=" << opt.buffer_size
            << ", queue_depth=" << opt.queue_depth << "\n";

  if (opt.mode == "latency" || opt.mode == "both") {
    syncAwait(run_latency(opt, payload));
  }
  if (opt.mode == "throughput" || opt.mode == "both") {
    syncAwait(run_throughput(opt, payload));
  }
  return 0;
}

int main(int argc, char** argv) {
  try {
    auto opt = parse_options(argc, argv);
    easylog::logger<>::instance().set_min_severity(opt.log_level);
    easylog::logger<>::instance().set_async(false);
    if (opt.role == "server") return run_server(opt);
    return run_client(opt);
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
}
