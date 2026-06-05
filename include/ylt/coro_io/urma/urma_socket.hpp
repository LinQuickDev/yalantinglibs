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

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

#include "asio/dispatch.hpp"
#include "asio/ip/address.hpp"
#include "asio/ip/tcp.hpp"
#include "async_simple/coro/Lazy.h"
#include "async_simple/util/move_only_function.h"
#include "ylt/coro_io/coro_io.hpp"
#include "ylt/coro_io/detail/circle_buffer.hpp"
#include "ylt/easylog.hpp"
#include "ylt/struct_pack.hpp"
#include "ylt/urma/urma_api.h"
#include "ylt/urma/urma_types.h"
#include "ylt/coro_io/urma/urma_device.hpp"

#define URMA_EID_LEN 16

namespace coro_io {
namespace detail {

// URMA buffer wrapper - local representation
struct urma_buf_t {
  void* addr = nullptr;
  size_t length = 0;
  urma_target_seg_t* seg = nullptr;  // URMA segment handle

  urma_sge_t to_sge() const {
    urma_sge_t sge;
    sge.addr = reinterpret_cast<uint64_t>(addr);
    sge.len = static_cast<uint32_t>(length);
    sge.tseg = seg;
    return sge;
  }

  explicit operator bool() const { return addr != nullptr && length > 0; }
};

using callback_t = async_simple::util::move_only_function<void(
    std::pair<std::error_code, std::size_t>)>;

// URMA CTP Socket state
struct urma_socket_shared_state_t
    : public std::enable_shared_from_this<urma_socket_shared_state_t> {
  static void resume(std::pair<std::error_code, std::size_t>&& arg,
                     callback_t&& handle) {
    if (handle) [[likely]] {
      auto handle_tmp = std::move(handle);
      handle_tmp(std::move(arg));
    }
  }

  coro_io::ExecutorWrapper<>* executor_;
  std::optional<asio::ip::tcp::socket> soc_;

  // URMA resources
  urma_context_t* urma_context_ = nullptr;
  urma_jfc_t* jfc_ = nullptr;
  urma_jfr_t* jfr_ = nullptr;
  urma_jetty_t* jetty_ = nullptr;
  urma_target_jetty_t* remote_jetty_ = nullptr;

  // Buffer management
  circle_buffer<urma_buf_t> recv_queue_;
  circle_buffer<urma_buf_t> send_queue_;
  circle_buffer<std::pair<std::error_code, std::size_t>> recv_result_;
  circle_buffer<callback_t> send_cb_;

  callback_t recv_cb_;
  urma_buf_t recv_buf_;

  std::size_t recv_buffer_cnt_ = 0;
  std::size_t send_buffer_cnt_ = 0;
  uint32_t buffer_size_ = 256 * 1024;  // Default 256KB

  std::atomic<bool> has_close_{false};
  bool peer_close_ = false;
  std::optional<async_simple::Promise<std::error_code>> wait_promise_;

  // Remote peer info
  std::array<uint8_t, URMA_EID_LEN> remote_eid_;
  uint32_t remote_jetty_id_ = 0;

  urma_socket_shared_state_t() = default;
  urma_socket_shared_state_t(coro_io::ExecutorWrapper<>* executor)
      : executor_(executor), soc_(std::in_place, executor->get_asio_executor()) {
    ELOG_DEBUG << "urma_socket_shared_state_t: executor=" << executor;
  }
  ~urma_socket_shared_state_t() { close(); }

  urma_socket_shared_state_t(urma_socket_shared_state_t&&) = delete;
  urma_socket_shared_state_t& operator=(urma_socket_shared_state_t&&) = delete;

  // Initialize URMA resources
  bool init(urma_context_t* ctx,
            coro_io::ExecutorWrapper<>* executor,
            std::size_t recv_buffer_cnt,
            std::size_t send_buffer_cnt,
            uint32_t buffer_size);

  void close();

  auto get_executor() const noexcept { return executor_; }

  asio::ip::tcp::socket& socket() noexcept { return *soc_; }

  // Post receive buffer
  void post_recv_impl(callback_t&& handler);

  // Post send with URMA buffer
  void post_send_impl(urma_buf_t buf, callback_t&& handler);

  // Poll for completions
  std::error_code poll_completion();

  // Register memory as URMA segment
  urma_buf_t register_buffer(void* addr, size_t len);

  // Unregister segment
  void unregister_buffer(urma_buf_t& buf);

  async_simple::coro::Lazy<std::error_code> waiting_write_over() {
    // TODO: fixme - waiting_write_over needs proper Future awaiting support
    co_return std::error_code{};
  }

  void wake_up_if_is_waiting(std::error_code ec) {
    if (wait_promise_) {
      auto promise = std::move(wait_promise_);
      wait_promise_ = std::nullopt;
      promise->setValue(ec);
    }
  }
};

}  // namespace detail

class urma_socket_t {
 public:
  // ASIO socket compatibility types
  using executor_type = asio::any_io_executor;
  using lowest_layer_type = urma_socket_t&;

  struct config_t {
    uint32_t cq_size = 128;
    uint16_t recv_buffer_cnt = 8;
    uint16_t send_buffer_cnt = 4;
    uint32_t buffer_size = 256 * 1024;  // 256KB default
    std::string device_name;
    int eid_index = 0;
  };

  // Socket info exchanged during handshake
  struct urma_socket_info {
    uint8_t eid[URMA_EID_LEN];
    uint32_t jetty_id;
    uint32_t buffer_size;
    constexpr static auto struct_pack_config = struct_pack::DISABLE_TYPE_INFO;
  };

  using callback_t = detail::callback_t;

  urma_socket_t() = default;
  urma_socket_t(ExecutorWrapper<>* executor, const config_t& config);
  urma_socket_t(const config_t& config);
  urma_socket_t(urma_socket_t&&) = default;
  urma_socket_t& operator=(urma_socket_t&&) = default;
  ~urma_socket_t() { close(); }

  bool is_open() const noexcept {
    return state_ != nullptr && !state_->has_close_;
  }

  std::size_t remain_read_buffer_size() const { return remain_data_.size(); }

  void set_read_buffer_len(std::size_t has_read_size, std::size_t remain_size) {
    remain_data_ = std::string_view{
        reinterpret_cast<char*>(state_->recv_buf_.addr) + has_read_size,
        remain_size};
  }

  uint32_t get_buffer_size() const noexcept { return buffer_size_; }
  config_t& get_config() noexcept { return conf_; }
  const config_t& get_config() const noexcept { return conf_; }

  // Connection management
  async_simple::coro::Lazy<std::error_code> connect(
      const std::string& addr, const std::string& port);

  // Connect with endpoint sequence (for coro_rpc compatibility)
  template <typename EndPointSeq>
  async_simple::coro::Lazy<std::error_code> connect(
      const EndPointSeq& endpoint) noexcept;

  async_simple::coro::Lazy<std::error_code> accept() noexcept;
  void prepare_accept(asio::ip::tcp::socket soc) noexcept;

  void close() noexcept;

  // Post receive
  void post_recv(callback_t&& cb) { state_->post_recv_impl(std::move(cb)); }

  // Post send
  void post_send(detail::urma_buf_t buf, callback_t&& cb) {
    state_->post_send_impl(buf, std::move(cb));
  }

  // Get executor
  auto get_executor() const { return state_->get_executor(); }
  executor_type get_executor() { return state_->get_executor()->get_asio_executor(); }

  // ASIO socket compatibility
  lowest_layer_type lowest_layer() { return *this; }
  lowest_layer_type lowest_layer() const { return const_cast<urma_socket_t&>(*this); }
  asio::ip::tcp::socket& next_layer() { return state_->socket(); }
  const asio::ip::tcp::socket& next_layer() const { return state_->socket(); }

  // ASIO async operations (delegate to internal TCP socket)
  template <typename MutableBuffers, typename CompletionToken>
  auto async_read_some(const MutableBuffers& buffers, CompletionToken&& token) {
    return state_->socket().async_read_some(buffers, std::forward<CompletionToken>(token));
  }
  template <typename ConstBuffers, typename CompletionToken>
  auto async_write_some(const ConstBuffers& buffers, CompletionToken&& token) {
    return asio::async_write(state_->socket(), buffers, std::forward<CompletionToken>(token));
  }
  std::error_code cancel() {
    std::error_code ec;
    state_->socket().cancel(ec);
    return ec;
  }

  // ASIO async_connect support (delegate to internal TCP socket)
  template <typename EndPointSeq, typename CompletionToken>
  auto async_connect(const EndPointSeq& endpoints, CompletionToken&& token) {
    return asio::async_connect(state_->socket(), endpoints, std::forward<CompletionToken>(token));
  }

  // Async connect with endpoint iterator (for ASIO range connectivity)
  template <typename Iterator, typename CompletionToken>
  auto async_connect(Iterator begin, Iterator end, CompletionToken&& token) {
    return asio::async_connect(state_->socket(), begin, end, std::forward<CompletionToken>(token));
  }

  // Direct address-based async_connect (for URMA compatibility)
  template <typename CompletionToken>
  auto async_connect(const std::string& host, const std::string& port, CompletionToken&& token) {
    return asio::async_connect(state_->socket(), host, port, std::forward<CompletionToken>(token));
  }

  // Remote address (for logging)
  std::string remote_address() const { return remote_address_; }
  uint32_t remote_jetty_id() const { return state_->remote_jetty_id_; }

  // Address helpers for socket_wrapper
  asio::ip::address get_remote_address() const {
    if (state_ && !remote_address_.empty()) {
      std::error_code ec;
      return asio::ip::make_address(remote_address_, ec);
    }
    return asio::ip::address{};
  }
  uint32_t get_remote_qp_num() const { return state_ ? state_->remote_jetty_id_ : 0; }
  asio::ip::address get_local_address() const {
    auto device = get_global_urma_device();
    if (device) {
      return device->gid_address();
    }
    return asio::ip::address{};
  }
  uint32_t get_local_qp_num() const { return state_ ? state_->jetty_->jetty_id.id : 0; }

  // Magic header for protocol identification
  constexpr static uint32_t urma_md5_header =
      struct_pack::get_type_code<urma_socket_info>();
  constexpr static char urma_md5_first_header =
      struct_pack::get_type_code<urma_socket_info>() % 256;

 private:
  detail::urma_socket_shared_state_t* state() const { return state_.get(); }

  config_t conf_;
  uint32_t buffer_size_ = 256 * 1024;
  std::string remote_address_;
  std::string_view remain_data_;
  std::unique_ptr<detail::urma_socket_shared_state_t> state_;
};

// Helper function to convert EID to address string
inline std::string eid_to_address(const uint8_t* eid) {
  char buf[64];
  snprintf(buf, sizeof(buf), "%d.%d.%d.%d", eid[0], eid[1], eid[2], eid[3]);
  return std::string(buf);
}

// Implementation

namespace detail {

inline bool urma_socket_shared_state_t::init(
    urma_context_t* ctx,
    coro_io::ExecutorWrapper<>* executor,
    std::size_t recv_buffer_cnt,
    std::size_t send_buffer_cnt,
    uint32_t buffer_size) {
  urma_context_ = ctx;
  executor_ = executor;
  recv_buffer_cnt_ = recv_buffer_cnt;
  send_buffer_cnt_ = send_buffer_cnt;
  buffer_size_ = buffer_size;

  // Initialize socket with executor
  soc_.emplace(executor->get_asio_executor());

  // Create JFC (Completion Channel) - polling mode (jfce = nullptr)
  urma_jfc_cfg_t jfc_cfg = {};
  jfc_cfg.depth = 64;
  jfc_cfg.flag.value = 0;
  jfc_cfg.jfce = nullptr;  // polling mode
  jfc_cfg.user_ctx = 0;
  jfc_ = urma_create_jfc(ctx, &jfc_cfg);
  if (!jfc_) {
    ELOG_ERROR << "Failed to create JFC";
    return false;
  }

  // Create JFR (Receive Queue) for CTP mode
  urma_jfr_cfg_t jfr_cfg = {};
  jfr_cfg.depth = static_cast<uint32_t>(recv_buffer_cnt);
  jfr_cfg.flag.value = 0;
  jfr_cfg.trans_mode = URMA_TM_RM;  // CTP uses RM mode
  jfr_cfg.max_sge = 1;
  jfr_cfg.min_rnr_timer = 12;  // typical value
  jfr_cfg.jfc = jfc_;
  jfr_cfg.token_value = {};  // empty token
  jfr_ = urma_create_jfr(ctx, &jfr_cfg);
  if (!jfr_) {
    ELOG_ERROR << "Failed to create JFR";
    return false;
  }

  // Create Jetty with shared JFR (CTP mode)
  urma_jetty_cfg_t jetty_cfg = {};
  jetty_cfg.flag.bs.share_jfr = 1;  // CTP requires shared JFR
  jetty_cfg.jfs_cfg.depth = static_cast<uint32_t>(send_buffer_cnt + 1);
  jetty_cfg.jfs_cfg.flag.value = 0;
  jetty_cfg.jfs_cfg.trans_mode = URMA_TM_RM;  // CTP uses RM mode
  jetty_cfg.jfs_cfg.jfc = jfc_;
  jetty_cfg.jfs_cfg.user_ctx = 0;
  jetty_cfg.shared.jfr = jfr_;
  jetty_ = urma_create_jetty(ctx, &jetty_cfg);
  if (!jetty_) {
    ELOG_ERROR << "Failed to create Jetty";
    return false;
  }

  // Initialize buffers
  new (&recv_queue_) circle_buffer<urma_buf_t>(recv_buffer_cnt);
  new (&send_queue_) circle_buffer<urma_buf_t>(send_buffer_cnt);
  new (&recv_result_) circle_buffer<std::pair<std::error_code, std::size_t>>(recv_buffer_cnt);
  new (&send_cb_) circle_buffer<callback_t>(send_buffer_cnt + 2);

  // Get Jetty ID for connection establishment
  // Jetty ID is accessed directly from the jetty structure
  uint32_t jetty_id = jetty_->jetty_id.id;
  ELOG_INFO << "URMA socket init: Jetty ID=" << jetty_id 
           << " buffer_size=" << buffer_size_
           << " recv_buffer_cnt=" << recv_buffer_cnt_
           << " send_buffer_cnt=" << send_buffer_cnt_;
  return true;
}

inline void urma_socket_shared_state_t::close() {
  ELOG_DEBUG << "URMA socket close: Jetty ID=" << (jetty_ ? jetty_->jetty_id.id : 0);
  if (has_close_.exchange(true)) {
    return;
  }

  if (jetty_) {
    urma_delete_jetty(jetty_);
    jetty_ = nullptr;
  }
  if (jfr_) {
    urma_delete_jfr(jfr_);
    jfr_ = nullptr;
  }
  if (jfc_) {
    urma_delete_jfc(jfc_);
    jfc_ = nullptr;
  }
}

inline urma_buf_t urma_socket_shared_state_t::register_buffer(void* addr, size_t len) {
  urma_buf_t buf;
  buf.addr = addr;
  buf.length = len;

  urma_seg_cfg_t seg_cfg = {};
  seg_cfg.va = reinterpret_cast<uint64_t>(addr);
  seg_cfg.len = len;
  seg_cfg.flag.bs.access = URMA_ACCESS_READ | URMA_ACCESS_WRITE;
  seg_cfg.flag.bs.cacheable = URMA_NON_CACHEABLE;
  seg_cfg.flag.bs.token_policy = URMA_TOKEN_NONE;

  buf.seg = urma_register_seg(urma_context_, &seg_cfg);
  if (!buf.seg) {
    ELOG_ERROR << "Failed to register segment";
    buf.addr = nullptr;
  }
  return buf;
}

inline void urma_socket_shared_state_t::unregister_buffer(urma_buf_t& buf) {
  if (buf.seg) {
    urma_unregister_seg(buf.seg);
    buf.seg = nullptr;
  }
  buf.addr = nullptr;
  buf.length = 0;
}

inline void urma_socket_shared_state_t::post_recv_impl(callback_t&& handler) {
  ELOG_TRACE << "URMA post_recv: queue_size=" << recv_result_.size() << " closed=" << has_close_.load();
  if (!recv_result_.empty()) {
    auto result = recv_result_.pop();
    recv_buf_ = std::move(recv_queue_.pop());
    urma_socket_shared_state_t::resume(std::move(result), std::move(handler));
    return;
  }
  else if (has_close_) [[unlikely]] {
    ELOG_WARN << "URMA post_recv: socket already closed";
    urma_socket_shared_state_t::resume(
        std::pair{std::make_error_code(std::errc::io_error), 0},
        std::move(handler));
    return;
  }
  recv_cb_ = std::move(handler);
}

inline void urma_socket_shared_state_t::post_send_impl(urma_buf_t buf, callback_t&& handler) {
  if (has_close_) [[unlikely]] {
    ELOG_WARN << "URMA post_send: socket closed, buf_size=" << buf.length;
    urma_socket_shared_state_t::resume(
        std::pair{std::make_error_code(std::errc::operation_canceled), 0},
        std::move(handler));
    return;
  }
  ELOG_TRACE << "URMA post_send: buf_size=" << buf.length << " remote_jetty_id=" << (remote_jetty_ ? remote_jetty_id_ : 0);

  // Build URMA SEND work request
  urma_sge_t sge = buf.to_sge();
  urma_sg_t src_sg;
  src_sg.sge = &sge;
  src_sg.num_sge = 1;

  urma_send_wr_t send_wr = {};
  send_wr.src = src_sg;

  urma_jfs_wr_t jfs_wr = {};
  jfs_wr.opcode = URMA_OPC_SEND;
  jfs_wr.send = send_wr;
  jfs_wr.tjetty = remote_jetty_;
  jfs_wr.user_ctx = reinterpret_cast<uint64_t>(new callback_t(std::move(handler)));

  urma_jfs_wr* bad_wr = nullptr;
  auto status = urma_post_jetty_send_wr(jetty_, &jfs_wr, &bad_wr);
  if (status != 0) [[unlikely]] {
    delete reinterpret_cast<callback_t*>(jfs_wr.user_ctx);
    auto err_code = std::make_error_code(std::errc{std::abs(status)});
    ELOG_ERROR << "URMA post send failed: " << err_code.message();
    urma_socket_shared_state_t::resume(std::pair{err_code, std::size_t{0}},
                                        std::move(handler));
  } else {
    send_cb_.push(callback_t{});  // Placeholder
  }
}

inline std::error_code urma_socket_shared_state_t::poll_completion() {
  urma_cr_t cr_list[8];
  int num_completed = urma_poll_jfc(jfc_, 8, cr_list);
  ELOG_TRACE << "URMA poll: num_completed=" << num_completed;

  if (num_completed < 0) [[unlikely]] {
    return std::make_error_code(std::errc::io_error);
  }

  std::error_code ec;
  for (int i = 0; i < num_completed; ++i) {
    auto& cr = cr_list[i];
    ec = (cr.status == 0) ? std::error_code{}
                            : std::make_error_code(std::errc::io_error);

    if (cr.status != 0) [[unlikely]] {
      ELOG_WARN << "URMA operation failed with status: " << cr.status;
    }

    // Determine if send or recv completion based on CR flag
    if (cr.flag.bs.s_r == 0) {  // Send completion
      if (!send_cb_.empty()) {
        urma_socket_shared_state_t::resume(
            std::pair{ec, static_cast<std::size_t>(cr.completion_len)},
            send_cb_.pop());
      }
    } else {  // Receive completion
      if (!recv_cb_) {
        recv_result_.push(
            std::pair{ec, static_cast<std::size_t>(cr.completion_len)});
      } else {
        recv_buf_ = std::move(recv_queue_.pop());
        urma_socket_shared_state_t::resume(
            std::pair{ec, static_cast<std::size_t>(cr.completion_len)},
            std::move(recv_cb_));
      }
    }
  }

  return {};
}

}  // namespace detail

// urma_socket_t implementation

inline urma_socket_t::urma_socket_t(const config_t& config)
    : conf_(config), buffer_size_(config.buffer_size) {}

inline void urma_socket_t::close() noexcept {
  if (state_) {
    state_->close();
    state_.reset();
  }
}

inline async_simple::coro::Lazy<std::error_code> urma_socket_t::connect(
    const std::string& addr, const std::string& port) {
  // Get executor via CurrentExecutor
  auto executor = co_await async_simple::CurrentExecutor{};
  if (!executor) {
    co_return std::make_error_code(std::errc::operation_not_supported);
  }

  auto exec = static_cast<coro_io::ExecutorWrapper<>*>(executor->checkout());
  state_ = std::make_unique<detail::urma_socket_shared_state_t>(exec);
  state_->executor_ = exec;

  // Get global URMA device
  auto device = get_global_urma_device();
  if (!device || !device->is_valid()) {
    co_return std::make_error_code(std::errc::network_unreachable);
  }

  // Initialize URMA resources
  if (!state_->init(device->context(),
                   state_->executor_,
                   conf_.recv_buffer_cnt,
                   conf_.send_buffer_cnt,
                   conf_.buffer_size)) {
    state_.reset();
    co_return std::make_error_code(std::errc::operation_not_supported);
  }

  // TCP handshake for connection establishment using coro_io async_connect
  auto ec = co_await coro_io::async_connect(state_->executor_, *state_->soc_, addr, port);
  if (ec) [[unlikely]] {
    co_return ec;
  }

  // Exchange socket info
  urma_socket_info local_info{};
  local_info.jetty_id = state_->jetty_->jetty_id.id;
  local_info.buffer_size = conf_.buffer_size;
  // Get local EID from device
  auto& local_eid = device->eid();
  std::memcpy(local_info.eid, local_eid.raw, URMA_EID_LEN);

  // Send our info
  char buffer[sizeof(urma_socket_info)];
  std::memcpy(buffer, &local_info, sizeof(local_info));
  co_await async_write(*state_->soc_, asio::buffer(buffer));

  // Receive peer info
  urma_socket_info peer_info;
  auto [ec2, _] = co_await async_read(*state_->soc_, asio::buffer(buffer, sizeof(buffer)));
  if (ec2) [[unlikely]] {
    co_return ec2;
  }
  std::memcpy(&peer_info, buffer, sizeof(peer_info));

  // Import remote jetty
  urma_rjetty_t remote_jetty_id = {};
  remote_jetty_id.jetty_id.id = peer_info.jetty_id;
  remote_jetty_id.jetty_id.eid = *reinterpret_cast<urma_eid_t*>(peer_info.eid);
  remote_jetty_id.trans_mode = URMA_TM_RM;
  remote_jetty_id.type = URMA_JETTY;
  remote_jetty_id.tp_type = URMA_CTP;  // CTP mode
  remote_jetty_id.flag.value = 0;

  state_->remote_jetty_ = urma_import_jetty(state_->urma_context_, &remote_jetty_id, nullptr);
  if (!state_->remote_jetty_) {
    co_return std::make_error_code(std::errc::operation_not_supported);
  }

  remote_address_ = eid_to_address(peer_info.eid);
  state_->remote_jetty_id_ = peer_info.jetty_id;

  ELOG_INFO << "Connected to URMA peer, Jetty ID: " << peer_info.jetty_id;
  co_return std::error_code{};
}

template <typename EndPointSeq>
inline async_simple::coro::Lazy<std::error_code> urma_socket_t::connect(
    const EndPointSeq& endpoint) noexcept {
  // Get executor via CurrentExecutor
  auto executor = co_await async_simple::CurrentExecutor{};
  if (!executor) {
    co_return std::make_error_code(std::errc::operation_not_supported);
  }

  auto exec = static_cast<coro_io::ExecutorWrapper<>*>(executor->checkout());
  state_ = std::make_unique<detail::urma_socket_shared_state_t>(exec);
  state_->executor_ = exec;

  // Get global URMA device
  auto device = get_global_urma_device();
  if (!device || !device->is_valid()) {
    co_return std::make_error_code(std::errc::network_unreachable);
  }

  // Initialize URMA resources
  if (!state_->init(device->context(),
                    state_->executor_,
                    conf_.recv_buffer_cnt,
                    conf_.send_buffer_cnt,
                    conf_.buffer_size)) {
    state_.reset();
    co_return std::make_error_code(std::errc::operation_not_supported);
  }

  // TCP handshake for connection establishment using coro_io async_connect
  auto ec = co_await coro_io::async_connect(state_->socket(), endpoint);
  if (ec) [[unlikely]] {
    co_return ec;
  }

  // Exchange socket info
  urma_socket_info local_info{};
  local_info.jetty_id = state_->jetty_->jetty_id.id;
  local_info.buffer_size = conf_.buffer_size;
  auto& local_eid = device->eid();
  std::memcpy(local_info.eid, local_eid.raw, URMA_EID_LEN);

  // Send our info
  char buffer[sizeof(urma_socket_info)];
  std::memcpy(buffer, &local_info, sizeof(local_info));
  co_await async_write(*state_->soc_, asio::buffer(buffer));

  // Receive peer info
  urma_socket_info peer_info;
  auto [ec2, _] = co_await async_read(*state_->soc_, asio::buffer(buffer, sizeof(buffer)));
  if (ec2) [[unlikely]] {
    co_return ec2;
  }
  std::memcpy(&peer_info, buffer, sizeof(peer_info));

  // Import remote jetty
  urma_rjetty_t remote_jetty_id = {};
  remote_jetty_id.jetty_id.id = peer_info.jetty_id;
  remote_jetty_id.jetty_id.eid = *reinterpret_cast<urma_eid_t*>(peer_info.eid);
  remote_jetty_id.trans_mode = URMA_TM_RM;
  remote_jetty_id.type = URMA_JETTY;
  remote_jetty_id.tp_type = URMA_CTP;  // CTP mode
  remote_jetty_id.flag.value = 0;

  state_->remote_jetty_ = urma_import_jetty(state_->urma_context_, &remote_jetty_id, nullptr);
  if (!state_->remote_jetty_) {
    co_return std::make_error_code(std::errc::operation_not_supported);
  }

  remote_address_ = eid_to_address(peer_info.eid);
  state_->remote_jetty_id_ = peer_info.jetty_id;

  ELOG_INFO << "Connected to URMA peer via endpoint, Jetty ID: " << peer_info.jetty_id;
  co_return std::error_code{};
}

inline async_simple::coro::Lazy<std::error_code> urma_socket_t::accept() noexcept {
  // Get executor via CurrentExecutor
  auto executor = co_await async_simple::CurrentExecutor{};
  if (!executor) {
    co_return std::make_error_code(std::errc::operation_not_supported);
  }

  auto exec = static_cast<coro_io::ExecutorWrapper<>*>(executor->checkout());
  state_ = std::make_unique<detail::urma_socket_shared_state_t>(exec);
  state_->executor_ = exec;

  // Get global URMA device
  auto device = get_global_urma_device();
  if (!device || !device->is_valid()) {
    co_return std::make_error_code(std::errc::network_unreachable);
  }

  // Initialize URMA resources
  if (!state_->init(device->context(),
                   state_->executor_,
                   conf_.recv_buffer_cnt,
                   conf_.send_buffer_cnt,
                   conf_.buffer_size)) {
    state_.reset();
    co_return std::make_error_code(std::errc::operation_not_supported);
  }

  // Receive peer info
  char buffer[sizeof(urma_socket_info)];
  auto [ec, _] = co_await async_read(*state_->soc_, asio::buffer(buffer));
  if (ec) [[unlikely]] {
    co_return ec;
  }

  urma_socket_info peer_info;
  std::memcpy(&peer_info, buffer, sizeof(peer_info));

  // Import remote jetty
  urma_rjetty_t remote_jetty_id = {};
  remote_jetty_id.jetty_id.id = peer_info.jetty_id;
  remote_jetty_id.jetty_id.eid = *reinterpret_cast<urma_eid_t*>(peer_info.eid);
  remote_jetty_id.trans_mode = URMA_TM_RM;
  remote_jetty_id.type = URMA_JETTY;
  remote_jetty_id.tp_type = URMA_CTP;  // CTP mode
  remote_jetty_id.flag.value = 0;

  state_->remote_jetty_ = urma_import_jetty(state_->urma_context_, &remote_jetty_id, nullptr);
  if (!state_->remote_jetty_) {
    co_return std::make_error_code(std::errc::operation_not_supported);
  }

  // Send our info
  urma_socket_info local_info{};
  local_info.jetty_id = state_->jetty_->jetty_id.id;
  local_info.buffer_size = conf_.buffer_size;
  auto& local_eid = device->eid();
  std::memcpy(local_info.eid, local_eid.raw, URMA_EID_LEN);

  std::memcpy(buffer, &local_info, sizeof(local_info));
  co_await async_write(*state_->soc_, asio::buffer(buffer));

  remote_address_ = eid_to_address(peer_info.eid);
  state_->remote_jetty_id_ = peer_info.jetty_id;

  ELOG_INFO << "Accepted URMA connection from peer, Jetty ID: " << peer_info.jetty_id;
  co_return std::error_code{};
}

inline urma_socket_t::urma_socket_t(ExecutorWrapper<>* executor, const config_t& config)
    : conf_(config), buffer_size_(config.buffer_size) {
  ELOG_DEBUG << "urma_socket_t(executor, config): executor=" << executor << " buffer_size=" << config.buffer_size;
  if (!executor) {
    ELOG_ERROR << "URMA urma_socket_t: executor is nullptr";
    return;
  }
  auto exec = static_cast<coro_io::ExecutorWrapper<>*>(executor->checkout());
  if (!exec) {
    ELOG_ERROR << "URMA urma_socket_t: checkout() returned nullptr";
    return;
  }
  state_ = std::make_unique<detail::urma_socket_shared_state_t>(exec);
  state_->executor_ = exec;
}

inline void urma_socket_t::prepare_accept(asio::ip::tcp::socket soc) noexcept {
  if (!state_) {
    state_ = std::make_unique<detail::urma_socket_shared_state_t>();
  }
  if (!state_->soc_.has_value()) {
    state_->soc_.emplace(soc.get_executor());
  }
  *state_->soc_ = std::move(soc);
}
#ifdef YLT_ENABLE_URMA
template <typename EndPointSeq>
inline async_simple::coro::Lazy<std::error_code> async_connect(
    urma_socket_t &socket, const EndPointSeq &endpoint) noexcept {
  return socket.connect(endpoint);
}
#endif


}  // namespace coro_io