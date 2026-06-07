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
 * WITHOUT WARRANTIES OF CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <queue>
#include <mutex>
#include <system_error>
#include <unistd.h>
#include <vector>

#include "ylt/easylog.hpp"

#ifdef YLT_ENABLE_URMA
#include "ylt/urma/urma_api.h"
#endif

namespace coro_io {

struct urma_buffer_t {
  void* addr = nullptr;
  size_t length = 0;
  void* seg = nullptr;  // urma_target_seg_t*

  urma_buffer_t() = default;
  urma_buffer_t(void* a, size_t l, void* s) : addr(a), length(l), seg(s) {}
  explicit operator bool() const { return addr != nullptr && length > 0; }
};

class urma_buffer_pool_t {
 public:
  urma_buffer_pool_t(void* ctx, size_t buffer_size, size_t buffer_count);
  ~urma_buffer_pool_t();

  urma_buffer_pool_t(const urma_buffer_pool_t&) = delete;
  urma_buffer_pool_t& operator=(const urma_buffer_pool_t&) = delete;

  urma_buffer_t get_buffer(int gpu_id = -1);
  void return_buffer(urma_buffer_t& buffer);

  size_t buffer_size() const { return config_.buffer_size; }
  size_t total_buffer_count() const { return config_.buffer_count; }
  size_t free_buffer_count() const;
  bool memory_out_of_limit() const { return free_buffer_count() == 0; }
  void* context() const { return ctx_; }

  struct Config {
    size_t buffer_size = 4 * 1024;
    size_t buffer_count = 8;
    int gpu_id = -1;
  };
  const Config& get_config() const { return config_; }

 private:
  bool init_buffers();

 private:
  void* ctx_ = nullptr;
  Config config_;
  void* base_addr_ = nullptr;
  size_t allocation_size_ = 0;
  void* seg_ = nullptr;  // urma_target_seg_t*
  std::vector<urma_buffer_t> buffers_;
  std::queue<size_t> free_indices_;
  mutable std::mutex mutex_;
  std::atomic<size_t> outstanding_buffers_{0};
};

// ============= Implementation (inline in header) =============

inline urma_buffer_pool_t::urma_buffer_pool_t(void* ctx, size_t buffer_size,
                                             size_t buffer_count)
    : ctx_(ctx), config_({buffer_size, buffer_count, -1}) {
  init_buffers();
}

inline bool urma_buffer_pool_t::init_buffers() {
#ifdef YLT_ENABLE_URMA
  auto init_begin = std::chrono::steady_clock::now();
  buffers_.reserve(config_.buffer_count);

  long page_size_value = ::sysconf(_SC_PAGESIZE);
  size_t page_size =
      page_size_value > 0 ? static_cast<size_t>(page_size_value) : 4096;
  if (config_.buffer_size == 0 || config_.buffer_count == 0) return false;
  if (config_.buffer_count >
      std::numeric_limits<size_t>::max() / config_.buffer_size) {
    ELOG_ERROR << "URMA buffer pool size overflow: buffer_size="
               << config_.buffer_size
               << ", buffer_count=" << config_.buffer_count;
    return false;
  }
  allocation_size_ = config_.buffer_size * config_.buffer_count;
  int alloc_status =
      ::posix_memalign(&base_addr_, page_size, allocation_size_);
  if (alloc_status != 0) {
    ELOG_ERROR << "Failed to allocate page-aligned URMA buffer pool: "
               << std::error_code(alloc_status, std::generic_category())
                      .message()
               << ", alignment=" << page_size
               << ", size=" << allocation_size_;
    return false;
  }

  urma_reg_seg_flag_t flag = {};
  flag.bs.token_policy = URMA_TOKEN_NONE;
  flag.bs.cacheable = URMA_NON_CACHEABLE;
  flag.bs.access = URMA_ACCESS_READ | URMA_ACCESS_WRITE | URMA_ACCESS_ATOMIC;
  flag.bs.token_id_valid = 0;

  urma_seg_cfg_t seg_cfg = {};
  seg_cfg.va = reinterpret_cast<uint64_t>(base_addr_);
  seg_cfg.len = allocation_size_;
  seg_cfg.token_id = nullptr;
  seg_cfg.token_value = {};
  seg_cfg.flag = flag;
  seg_cfg.user_ctx = reinterpret_cast<uint64_t>(base_addr_);
  seg_cfg.iova = 0;

  errno = 0;
  seg_ = urma_register_seg(reinterpret_cast<urma_context_t*>(ctx_), &seg_cfg);
  if (!seg_) {
    auto error = std::error_code(errno, std::generic_category());
    ELOG_ERROR << "urma_register_seg failed: errno=" << errno << " ("
               << error.message() << ")"
               << ", address=" << base_addr_ << ", length=" << seg_cfg.len
               << ", alignment=" << page_size << ", access=" << flag.bs.access;
    std::free(base_addr_);
    base_addr_ = nullptr;
    allocation_size_ = 0;
    return false;
  }

  auto* base = static_cast<char*>(base_addr_);
  for (size_t i = 0; i < config_.buffer_count; ++i) {
    buffers_.push_back(urma_buffer_t(base + i * config_.buffer_size,
                                     config_.buffer_size, seg_));
    free_indices_.push(i);
  }

  ELOG_INFO << "URMA buffer pool: " << buffers_.size()
            << " buffers of " << config_.buffer_size
            << " bytes, one registered segment of " << allocation_size_
            << " bytes, init_cost_us="
            << (std::chrono::steady_clock::now() - init_begin) /
                   std::chrono::microseconds(1);
  return !buffers_.empty();
#else
  return false;
#endif
}

inline urma_buffer_pool_t::~urma_buffer_pool_t() {
#ifdef YLT_ENABLE_URMA
  std::lock_guard<std::mutex> lock(mutex_);
  if (seg_) {
    urma_unregister_seg(static_cast<urma_target_seg_t*>(seg_));
    seg_ = nullptr;
  }
  if (base_addr_) {
    std::free(base_addr_);
    base_addr_ = nullptr;
  }
  allocation_size_ = 0;
  buffers_.clear();
#endif
}

inline urma_buffer_t urma_buffer_pool_t::get_buffer(int gpu_id) {
#ifdef YLT_ENABLE_URMA
  std::lock_guard<std::mutex> lock(mutex_);
  if (free_indices_.empty()) {
    ELOG_WARN << "URMA buffer pool out of buffers";
    return urma_buffer_t{};
  }
  size_t idx = free_indices_.front();
  free_indices_.pop();
  outstanding_buffers_++;
  return buffers_[idx];
#else
  return urma_buffer_t{};
#endif
}

inline void urma_buffer_pool_t::return_buffer(urma_buffer_t& buffer) {
#ifdef YLT_ENABLE_URMA
  if (!buffer) return;
  std::lock_guard<std::mutex> lock(mutex_);
  auto* base = static_cast<char*>(base_addr_);
  auto* addr = static_cast<char*>(buffer.addr);
  if (base && addr >= base && addr < base + allocation_size_) {
    auto offset = static_cast<size_t>(addr - base);
    if (offset % config_.buffer_size == 0) {
      auto index = offset / config_.buffer_size;
      if (index < buffers_.size()) {
        free_indices_.push(index);
        outstanding_buffers_--;
        buffer = urma_buffer_t{};
        return;
      }
    }
  }
  ELOG_WARN << "return unknown URMA buffer: " << buffer.addr;
#endif
}

inline size_t urma_buffer_pool_t::free_buffer_count() const {
#ifdef YLT_ENABLE_URMA
  std::lock_guard<std::mutex> lock(mutex_);
  return free_indices_.size();
#else
  return 0;
#endif
}

}  // namespace coro_io
