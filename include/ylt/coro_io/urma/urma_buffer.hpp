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
#include <cstdint>
#include <cstddef>
#include <memory>
#include <queue>
#include <vector>
#include <mutex>

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
    size_t buffer_size = 256 * 1024;
    size_t buffer_count = 8;
    int gpu_id = -1;
  };
  const Config& get_config() const { return config_; }

 private:
  bool init_buffers();

 private:
  void* ctx_ = nullptr;
  Config config_;
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
  buffers_.reserve(config_.buffer_count);

  for (size_t i = 0; i < config_.buffer_count; ++i) {
    void* addr = std::malloc(config_.buffer_size);
    if (!addr) {
      ELOG_ERROR << "Failed to allocate buffer";
      break;
    }

    urma_reg_seg_flag_t flag = {};
    flag.bs.token_policy = URMA_TOKEN_NONE;
    flag.bs.cacheable = URMA_NON_CACHEABLE;
    flag.bs.access = URMA_ACCESS_READ | URMA_ACCESS_WRITE;
    flag.bs.token_id_valid = 0;

    urma_seg_cfg_t seg_cfg = {};
    seg_cfg.va = reinterpret_cast<uint64_t>(addr);
    seg_cfg.len = config_.buffer_size;
    seg_cfg.flag = flag;

    urma_target_seg_t* seg = urma_register_seg(
        reinterpret_cast<urma_context_t*>(ctx_), &seg_cfg);
    if (!seg) {
      ELOG_ERROR << "urma_register_seg failed";
      std::free(addr);
      break;
    }

    buffers_.push_back(urma_buffer_t(addr, config_.buffer_size, seg));
    free_indices_.push(i);
  }

  ELOG_INFO << "URMA buffer pool: " << buffers_.size()
            << " buffers of " << config_.buffer_size << " bytes";
  return !buffers_.empty();
#else
  return false;
#endif
}

inline urma_buffer_pool_t::~urma_buffer_pool_t() {
#ifdef YLT_ENABLE_URMA
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& buf : buffers_) {
    if (buf) {
      if (buf.seg) urma_unregister_seg(static_cast<urma_target_seg_t*>(buf.seg));
      std::free(buf.addr);
    }
  }
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
  for (size_t i = 0; i < buffers_.size(); ++i) {
    if (buffers_[i].addr == buffer.addr) {
      free_indices_.push(i);
      outstanding_buffers_--;
      buffer = urma_buffer_t{};
      return;
    }
  }
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
