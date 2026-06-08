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
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <queue>
#include <mutex>
#include <system_error>
#include <sys/mman.h>
#include <thread>
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
  size_t total_memory_size() const { return allocation_size_; }
  size_t free_buffer_count() const;
  size_t outstanding_buffer_count() const {
    return outstanding_buffers_.load(std::memory_order_relaxed);
  }
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
#ifdef YLT_ENABLE_URMA
  std::vector<urma_target_seg_t> slice_segs_;
#endif
  std::vector<uint8_t> in_use_;
  static constexpr size_t shard_count_ = 64;
  size_t shard_for_index(size_t index) const noexcept {
    return index % shard_count_;
  }
  size_t preferred_shard() const noexcept {
    auto value = std::hash<std::thread::id>{}(std::this_thread::get_id());
    return value % shard_count_;
  }
  std::array<std::queue<size_t>, shard_count_> free_indices_;
  mutable std::array<std::mutex, shard_count_> mutexes_;
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
  auto requested_size = config_.buffer_size * config_.buffer_count;
  allocation_size_ =
      (requested_size + page_size - 1) / page_size * page_size;
  base_addr_ = ::mmap(nullptr, allocation_size_, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (base_addr_ == MAP_FAILED) {
    auto error = std::error_code(errno, std::generic_category());
    ELOG_ERROR << "Failed to mmap page-aligned URMA buffer pool: errno="
               << errno << " (" << error.message() << ")"
               << ", size=" << allocation_size_;
    base_addr_ = nullptr;
    allocation_size_ = 0;
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
    ::munmap(base_addr_, allocation_size_);
    base_addr_ = nullptr;
    allocation_size_ = 0;
    return false;
  }

  auto* base = static_cast<char*>(base_addr_);
  in_use_.assign(config_.buffer_count, 0);
#ifdef YLT_ENABLE_URMA
  slice_segs_.resize(config_.buffer_count);
#endif
  for (size_t i = 0; i < config_.buffer_count; ++i) {
    auto* addr = base + i * config_.buffer_size;
#ifdef YLT_ENABLE_URMA
    slice_segs_[i] = *static_cast<urma_target_seg_t*>(seg_);
    slice_segs_[i].seg.ubva.va = reinterpret_cast<uint64_t>(addr);
    slice_segs_[i].seg.len = config_.buffer_size;
    slice_segs_[i].user_ctx = reinterpret_cast<uint64_t>(addr);
    auto* buffer_seg = &slice_segs_[i];
#else
    auto* buffer_seg = seg_;
#endif
    buffers_.push_back(
        urma_buffer_t(addr, config_.buffer_size, buffer_seg));
    free_indices_[shard_for_index(i)].push(i);
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
  std::array<std::unique_lock<std::mutex>, shard_count_> locks;
  for (size_t i = 0; i < shard_count_; ++i) {
    locks[i] = std::unique_lock<std::mutex>(mutexes_[i]);
  }
  if (seg_) {
    urma_unregister_seg(static_cast<urma_target_seg_t*>(seg_));
    seg_ = nullptr;
  }
  if (base_addr_) {
    ::munmap(base_addr_, allocation_size_);
    base_addr_ = nullptr;
  }
  allocation_size_ = 0;
  buffers_.clear();
#ifdef YLT_ENABLE_URMA
  slice_segs_.clear();
#endif
  in_use_.clear();
#endif
}

inline urma_buffer_t urma_buffer_pool_t::get_buffer(int gpu_id) {
#ifdef YLT_ENABLE_URMA
  auto start = preferred_shard();
  for (size_t i = 0; i < shard_count_; ++i) {
    auto shard = (start + i) % shard_count_;
    std::lock_guard<std::mutex> lock(mutexes_[shard]);
    if (free_indices_[shard].empty()) continue;
    size_t idx = free_indices_[shard].front();
    free_indices_[shard].pop();
    if (idx < in_use_.size()) in_use_[idx] = 1;
    outstanding_buffers_++;
    return buffers_[idx];
  }
  ELOG_WARN << "URMA buffer pool out of buffers: total=" << buffers_.size()
            << ", outstanding="
            << outstanding_buffers_.load(std::memory_order_relaxed)
            << ", buffer_size=" << config_.buffer_size
            << ", total_memory=" << allocation_size_;
  return urma_buffer_t{};
#else
  return urma_buffer_t{};
#endif
}

inline void urma_buffer_pool_t::return_buffer(urma_buffer_t& buffer) {
#ifdef YLT_ENABLE_URMA
  if (!buffer) return;
  auto* base = static_cast<char*>(base_addr_);
  auto* addr = static_cast<char*>(buffer.addr);
  if (base && addr >= base && addr < base + allocation_size_) {
    auto offset = static_cast<size_t>(addr - base);
    if (offset % config_.buffer_size == 0) {
      auto index = offset / config_.buffer_size;
      if (index < buffers_.size()) {
        auto shard = shard_for_index(index);
        std::lock_guard<std::mutex> lock(mutexes_[shard]);
        if (index < in_use_.size() && !in_use_[index]) {
          ELOG_WARN << "return duplicated URMA buffer: " << buffer.addr
                    << ", index=" << index;
          buffer = urma_buffer_t{};
          return;
        }
        if (index < in_use_.size()) in_use_[index] = 0;
        free_indices_[shard].push(index);
        if (outstanding_buffers_.load(std::memory_order_relaxed) > 0)
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
  size_t total = 0;
  for (size_t i = 0; i < shard_count_; ++i) {
    std::lock_guard<std::mutex> lock(mutexes_[i]);
    total += free_indices_[i].size();
  }
  return total;
#else
  return 0;
#endif
}

}  // namespace coro_io
