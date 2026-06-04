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
#pragma once

#include <memory>
#include <string>
#include <vector>
#include <cstring>
#include <algorithm>

#include "ylt/easylog.hpp"

#ifdef YLT_ENABLE_URMA
#include "ylt/urma/urma_api.h"
#endif

namespace coro_io {

class urma_buffer_pool_t;

// URMA Device abstraction - wrapper around URMA library's ::urma_device_t
// Note: Using ::urma_device_t to explicitly refer to the URMA library type
class urma_device_wrapper_t {
 public:
  urma_device_wrapper_t();
  ~urma_device_wrapper_t();

  urma_device_wrapper_t(const urma_device_wrapper_t&) = delete;
  urma_device_wrapper_t& operator=(const urma_device_wrapper_t&) = delete;

  bool init(const std::string& device_name, int eid_index = 0);
  void close();

  urma_context_t* context() const { return context_; }
  urma_device_t* device() const { return device_ptr_; }
  const std::string& name() const { return name_; }
  int eid_index() const { return eid_index_; }
  const urma_eid_t& eid() const { return eid_; }
  uint32_t max_jetty() const { return device_attr_.dev_cap.max_jetty; }
  uint32_t max_jfc() const { return device_attr_.dev_cap.max_jfc; }

  std::string eid_string() const;
  asio::ip::address gid_address() const;
  bool is_valid() const { return context_ != nullptr && device_ptr_ != nullptr; }
  std::shared_ptr<urma_buffer_pool_t> get_buffer_pool() const { return buffer_pool_; }

 private:
  std::string name_;
  int eid_index_ = -1;
  urma_device_t* device_ptr_ = nullptr;  // URMA library device handle
  urma_context_t* context_ = nullptr;
  urma_eid_t eid_{};
  urma_device_attr_t device_attr_{};
  std::shared_ptr<urma_buffer_pool_t> buffer_pool_;
};

// Backward compatibility alias
using urma_device_t = urma_device_wrapper_t;

// Global device management
class urma_device_manager {
 public:
  static urma_device_manager& instance();
  bool init();
  std::shared_ptr<urma_device_wrapper_t> get_device(const std::string& device_name = "");
  std::vector<std::shared_ptr<urma_device_wrapper_t>> get_all_devices();
  std::shared_ptr<urma_device_wrapper_t> get_global_device();

 private:
  urma_device_manager() = default;
  ~urma_device_manager();
  bool initialized_ = false;
  std::vector<std::shared_ptr<urma_device_wrapper_t>> devices_;
  std::shared_ptr<urma_device_wrapper_t> global_device_;
};

inline std::shared_ptr<urma_device_wrapper_t> get_global_urma_device() {
  return urma_device_manager::instance().get_global_device();
}

// ============= Implementation (inline in header) =============

inline urma_device_wrapper_t::urma_device_wrapper_t() = default;

inline urma_device_wrapper_t::~urma_device_wrapper_t() { close(); }

inline bool urma_device_wrapper_t::init(const std::string& device_name, int eid_index) {
#ifdef YLT_ENABLE_URMA
  name_ = device_name;
  eid_index_ = eid_index;

  int num_devices = 0;
  // Use :: to explicitly call URMA library function
  ::urma_device_t** devices = urma_get_device_list(&num_devices);
  if (!devices || num_devices <= 0) {
    ELOG_ERROR << "urma_get_device_list failed";
    return false;
  }

  ::urma_device_t* found_device = nullptr;
  for (int i = 0; i < num_devices; ++i) {
    if (device_name.empty() || std::string(devices[i]->name) == device_name) {
      found_device = devices[i];
      break;
    }
  }

  if (!found_device) {
    ELOG_ERROR << "URMA device not found: " << device_name;
    urma_free_device_list(devices);
    return false;
  }

  device_ptr_ = found_device;

  uint32_t eid_cnt = 0;
  urma_eid_info_t* eid_list = urma_get_eid_list(device_ptr_, &eid_cnt);
  if (!eid_list || eid_cnt == 0) {
    ELOG_ERROR << "urma_get_eid_list failed";
    urma_free_device_list(devices);
    return false;
  }

  if (eid_index >= 0 && eid_index < (int)eid_cnt) {
    eid_index_ = eid_index;
  } else {
    eid_index_ = 0;
  }
  eid_ = eid_list[eid_index_].eid;
  urma_free_eid_list(eid_list);

  context_ = urma_create_context(device_ptr_, eid_index_);
  if (!context_) {
    ELOG_ERROR << "urma_create_context failed";
    urma_free_device_list(devices);
    return false;
  }

  if (urma_query_device(device_ptr_, &device_attr_) != 0) {
    ELOG_ERROR << "urma_query_device failed";
    urma_delete_context(context_);
    context_ = nullptr;
    urma_free_device_list(devices);
    return false;
  }

  urma_free_device_list(devices);
  ELOG_INFO << "URMA device: " << name_ << ", EID: " << eid_string();
  return true;
#else
  ELOG_WARN << "URMA not enabled";
  return false;
#endif
}

inline void urma_device_wrapper_t::close() {
#ifdef YLT_ENABLE_URMA
  if (context_) {
    urma_delete_context(context_);
    context_ = nullptr;
  }
#endif
}

inline std::string urma_device_wrapper_t::eid_string() const {
  char buf[64] = {0};
  snprintf(buf, sizeof(buf),
          "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:"
          "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
          eid_.raw[0], eid_.raw[1], eid_.raw[2], eid_.raw[3],
          eid_.raw[4], eid_.raw[5], eid_.raw[6], eid_.raw[7],
          eid_.raw[8], eid_.raw[9], eid_.raw[10], eid_.raw[11],
          eid_.raw[12], eid_.raw[13], eid_.raw[14], eid_.raw[15]);
  return std::string(buf);
}

inline asio::ip::address urma_device_wrapper_t::gid_address() const {
  char buf[64];
  snprintf(buf, sizeof(buf), "%d.%d.%d.%d",
          eid_.raw[0], eid_.raw[1], eid_.raw[2], eid_.raw[3]);
  std::error_code ec;
  auto addr = asio::ip::make_address(buf, ec);
  if (ec) return asio::ip::make_address_v4(0x7F000001);
  return addr;
}

// urma_device_manager
inline urma_device_manager& urma_device_manager::instance() {
  static urma_device_manager inst;
  return inst;
}

inline urma_device_manager::~urma_device_manager() {
  devices_.clear();
  global_device_.reset();
}

inline bool urma_device_manager::init() {
#ifdef YLT_ENABLE_URMA
  if (initialized_) return true;
  urma_init_attr_t init_attr = {};
  if (urma_init(&init_attr) != URMA_SUCCESS && urma_init(&init_attr) != URMA_EEXIST) {
    ELOG_ERROR << "urma_init failed";
    return false;
  }
  initialized_ = true;
  return true;
#else
  return false;
#endif
}

inline std::shared_ptr<urma_device_wrapper_t> urma_device_manager::get_device(
    const std::string& device_name) {
#ifdef YLT_ENABLE_URMA
  if (!initialized_) init();

  for (auto& dev : devices_) {
    if (device_name.empty() || dev->name() == device_name) {
      return dev;
    }
  }

  auto dev = std::make_shared<urma_device_wrapper_t>();
  if (!dev->init(device_name)) return nullptr;

  devices_.push_back(dev);
  if (!global_device_) global_device_ = dev;
  return dev;
#else
  return nullptr;
#endif
}

inline std::vector<std::shared_ptr<urma_device_wrapper_t>>
urma_device_manager::get_all_devices() {
#ifdef YLT_ENABLE_URMA
  if (!devices_.empty()) return devices_;
  if (!initialized_) init();

  int num_devices = 0;
  ::urma_device_t** urma_dev_list = urma_get_device_list(&num_devices);
  if (!urma_dev_list || num_devices <= 0) return devices_;

  for (int i = 0; i < num_devices; ++i) {
    auto dev = std::make_shared<urma_device_wrapper_t>();
    if (dev->init(urma_dev_list[i]->name)) {
      devices_.push_back(dev);
      if (!global_device_) global_device_ = dev;
    }
  }
  urma_free_device_list(urma_dev_list);
  return devices_;
#else
  return {};
#endif
}

inline std::shared_ptr<urma_device_wrapper_t> urma_device_manager::get_global_device() {
  if (!global_device_) get_all_devices();
  return global_device_;
}

}  // namespace coro_io