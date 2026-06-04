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
#include <queue>
#include <string>
#include <system_error>
#include <utility>

#include "asio/dispatch.hpp"
#include "asio/ip/address.hpp"
#include "asio/ip/tcp.hpp"
#include "async_simple/coro/Lazy.h"
#include "async_simple/util/move_only_function.h"
#include "ylt/coro_io/coro_io.hpp"
#include "ylt/easylog.hpp"
#include "ylt/struct_pack.hpp"

// URMA forward declarations
struct urma_context_t;
struct urma_jfc_t;
struct urma_jfr_t;
struct urma_jetty_t;
struct urma_target_jetty_t;
struct urma_target_seg_t;
struct urma_device_t;
struct urma_eid_t;
struct urma_cr_t;
struct urma_jfs_wr;
struct urma_seg_cfg_t;
struct urma_token_t;
struct urma_init_attr_t;

enum class urma_transport_type_t : int;

#define URMA_EID_LEN 16

namespace coro_io {
namespace detail {

struct urma_socket_shared_state_t;

template <typename T>
struct circle_buffer {
  std::vector<T> queue;
  uint32_t front_ = 0, end_ = 0;
  bool may_empty = true;
  circle_buffer(uint32_t size) {
    assert(size > 0);
    queue.resize(size);
  }
  void push(T&& elem) {
    assert(!full());
    may_empty = false;
    end_ = (end_ + 1) % queue.size();
    queue[end_] = std::move(elem);
  }
  T pop() {
    assert(!empty());
    front_ = (front_ + 1) % queue.size();
    may_empty = true;
    return std::move(queue[front_]);
  }
  T& back() { return queue[end_]; }
  T& front() { return queue[(front_ + 1) % queue.size()]; }
  bool full() const noexcept { return end_ == front_ && !may_empty; }
  bool empty() const noexcept { return end_ == front_ && may_empty; }
  std::size_t size() const noexcept {
    if (front_ > end_) {
      return queue.size() + end_ - front_;
    }
    else if (front_ == end_) {
      return empty() ? 0 : queue.size();
    }
    else {
      return end_ - front_;
    }
  }
};

// URMA-specific buffer representation (compatible with ibv_sge layout)
struct urma_sge {
  uint64_t addr;
  uint32_t length;
  uint32_t lkey;
};

struct urma_buffer_t {
  void* addr = nullptr;
  size_t length = 0;
  uint32_t lkey = 0;  // URMA key (used similarly to lkey)

  urma_sge subview(size_t offset = 0, size_t len = 0) const {
    urma_sge sge;
    sge.addr = reinterpret_cast<uint64_t>(
        reinterpret_cast<char*>(addr) + offset);
    sge.length = (len == 0) ? static_cast<uint32_t>(length - offset)
                             : static_cast<uint32_t>(len);
    sge.lkey = lkey;
    return sge;
  }

  explicit operator bool() const { return addr != nullptr && length > 0; }
};

using callback_t = async_simple::util::move_only_function<void(
    std::pair<std::error_code, std::size_t>)>;

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
  asio::ip::tcp::socket soc_;

  // URMA resources
  urma_context_t* urma_context_ = nullptr;
  urma_jfc_t* jfc_ = nullptr;
  urma_jfr_t* jfr_ = nullptr;
  urma_jetty_t* jetty_ = nullptr;
  urma_target_jetty_t* remote_jetty_ = nullptr;

  // Buffer management
  std::vector<urma_buffer_t> recv_buffers_;
  std::vector<urma_buffer_t> send_buffers_;
  circle_buffer<urma_buffer_t> recv_queue_;
  circle_buffer<urma_buffer_t> send_queue_;
  circle_buffer<std::pair<std::error_code, std::size_t>> recv_result_;
  circle_buffer<callback_t> send_cb_;

  callback_t recv_cb_;
  urma_buffer_t recv_buf_;

  std::size_t recv_buffer_cnt_ = 0;
  std::size_t send_buffer_cnt_ = 0;
  uint32_t send_buffer_data_size_ = 0;
  uint32_t buffer_size_ = 256 * 1024;  // Default 256KB

  std::atomic<bool> has_close_ = {false};
  bool peer_close_ = false;
  std::optional<async_simple::Promise<std::error_code>> wait_promise_;

  // Remote peer info
  std::array<uint8_t, URMA_EID_LEN> remote_eid_;
  uint32_t remote_jetty_id_ = 0;

  urma_socket_shared_state_t(coro_io::ExecutorWrapper<>* executor,
                             std::size_t recv_buffer_cnt,
                             std::size_t send_buffer_cnt,
                             std::size_t max_recv_buffer_cnt,
                             uint32_t buffer_size)
      : executor_(executor),
        soc_(executor->get_asio_executor()),
        recv_buffer_cnt_(recv_buffer_cnt),
        send_buffer_cnt_(send_buffer_cnt),
        buffer_size_(buffer_size),
        recv_queue_(max_recv_buffer_cnt),
        send_queue_(send_buffer_cnt + 1),
        recv_result_(max_recv_buffer_cnt),
        send_cb_(send_buffer_cnt + 2) {}

  urma_socket_shared_state_t(urma_socket_shared_state_t&&) = delete;
  urma_socket_shared_state_t& operator=(urma_socket_shared_state_t&&) = delete;

  auto get_executor() const noexcept { return executor_->get_asio_executor(); }

  void return_send_buffer(urma_buffer_t buffer) {
    assert(!send_queue_.full());
    send_queue_.push(std::move(buffer));
  }

  void wake_up_if_is_waiting(std::error_code ec) {
    if (wait_promise_) {
      auto promise = std::move(wait_promise_);
      wait_promise_ = std::nullopt;
      promise->setValue(ec);
    }
  }

  async_simple::coro::Lazy<std::error_code> waiting_write_over() {
    assert(send_cb_.size());
    wait_promise_ = async_simple::Promise<std::error_code>();
    auto ec = co_await wait_promise_->getFuture();
    co_return ec;
  }

  void cancel() {
    assert(executor_->get_asio_executor().running_in_this_thread());
    std::error_code ec;
    soc_.cancel(ec);
  }

  void close_impl() {
    ELOG_TRACE << "jetty closed";
    std::error_code ec;
    soc_.cancel(ec);
    soc_.close(ec);
  }

  void close(bool should_check = true) {
    assert(executor_->get_asio_executor().running_in_this_thread());

    bool has_close = false;
    if (should_check) {
      has_close = has_close_.exchange(true);
    }
    if (!has_close) {
      shutdown().start([self = shared_from_this()](auto&&) {
        self->close_impl();
      });
    }
  }

  async_simple::coro::Lazy<void> shutdown() {
    ELOG_TRACE << "start to notify peer close";
    co_await coro_io::sleep_for(std::chrono::seconds{1}, executor_);
    ELOG_TRACE << "finished to notify peer close";
    co_return;
  }

  urma_buffer_t release_send_buffer() noexcept {
    assert(send_queue_.size());
    send_buffer_data_size_ = 0;
    return send_queue_.pop();
  }

  std::size_t sent_request_count() const noexcept { return send_cb_.size(); }

  void post_send_impl(urma_sge sge, callback_t&& handler,
                      bool skip_check_close = false) {
    ELOG_TRACE << "post send sge length:" << sge.length
               << ", address:" << sge.addr;

    if (!skip_check_close && has_close_) [[unlikely]] {
      urma_socket_shared_state_t::resume(
          std::pair{std::make_error_code(std::errc::operation_canceled), 0},
          std::move(handler));
      return;
    }

    // Build URMA send WR
    urma_jfs_wr wr{};
    wr.next = nullptr;
    wr.sg_list = &sge;
    wr.num_sge = sge.length ? 1 : 0;
    wr.user_ctx = reinterpret_cast<uint64_t>(new callback_t(std::move(handler)));

    urma_jfs_wr* bad_wr = nullptr;
    auto status = urma_post_jetty_send_wr(jetty_, &wr, &bad_wr);
    if (status != 0) [[unlikely]] {
      delete reinterpret_cast<callback_t*>(wr.user_ctx);
      auto err_code = std::make_error_code(std::errc{std::abs(status)});
      ELOG_ERROR << "urma post send failed: " << err_code.message();
      urma_socket_shared_state_t::resume(std::pair{err_code, std::size_t{0}},
                                          std::move(handler));
    }
    else {
      send_cb_.push(callback_t{});  // Placeholder for now
    }
  }

  void post_recv_impl(callback_t&& handler) {
    if (!recv_result_.empty()) {
      auto result = recv_result_.pop();
      recv_buf_ = std::move(recv_queue_.pop());
      urma_socket_shared_state_t::resume(std::move(result), std::move(handler));
      return;
    }
    else if (has_close_) [[unlikely]] {
      urma_socket_shared_state_t::resume(
          std::pair{std::make_error_code(std::errc::io_error), 0},
          std::move(handler));
      return;
    }
    recv_cb_ = std::move(handler);
  }

  std::error_code poll_completion() {
    // Poll JFC for completions
    urma_cr_t cr_list[8];
    int num_completed = urma_poll_jfc(jfc_, 8, cr_list);

    if (num_completed < 0) [[unlikely]] {
      return std::make_error_code(std::errc::io_error);
    }

    std::error_code ec;
    for (int i = 0; i < num_completed; ++i) {
      auto& cr = cr_list[i];
      ec = (cr.status == 0) ? std::error_code{}
                             : std::make_error_code(std::errc::io_error);

      if (cr.status != 0) [[unlikely]] {
        ELOG_WARN << "urma operation failed with status:" << cr.status;
      }

      // Determine if this is a send or recv completion based on context
      if (!send_cb_.empty()) {
        urma_socket_shared_state_t::resume(
            std::pair{ec, static_cast<std::size_t>(cr.len)},
            send_cb_.pop());
      }
      else if (!recv_cb_) {
        recv_result_.push(
            std::pair{ec, static_cast<std::size_t>(cr.len)});
      }
      else {
        recv_buf_ = recv_queue_.pop();
        urma_socket_shared_state_t::resume(
            std::pair{ec, static_cast<std::size_t>(cr.len)},
            std::move(recv_cb_));
      }
    }

    return {};
  }
};

}  // namespace detail

class urma_socket_t {
 public:
  struct config_t {
    uint32_t cq_size = 128;
    uint16_t recv_buffer_cnt = 8;
    uint16_t send_buffer_cnt = 4;
    uint16_t jetty_cnt = 4;
    uint32_t buffer_size = 256 * 1024;  // 256KB default
    std::string device_name;
    int eid_index = 0;
    // Shared URMA context (can be nullptr for simple case)
    std::shared_ptr<void> urma_context;
  };

  // URMA socket info exchanged during handshake (similar to ib_socket_info)
  struct urma_socket_info {
    uint8_t eid[URMA_EID_LEN];      // EID
    uint32_t jetty_id;             // Jetty ID
    uint32_t buffer_size;          // Buffer size
    constexpr static auto struct_pack_config = struct_pack::DISABLE_TYPE_INFO;
  };

  using callback_t = detail::callback_t;

  urma_socket_t(coro_io::ExecutorWrapper<>* executor, const config_t& config)
      : executor_(executor) {
    init(config);
  }

  urma_socket_t(coro_io::ExecutorWrapper<>* executor = coro_io::get_global_executor())
      : executor_(executor) {
    init(config_t{});
  }

  urma_socket_t(const config_t& config)
      : executor_(coro_io::get_global_executor()) {
    init(config);
  }

  urma_socket_t(urma_socket_t&&) = default;
  urma_socket_t& operator=(urma_socket_t&& o) {
    close();
    remote_address_ = std::move(o.remote_address_);
    remote_jetty_id_ = o.remote_jetty_id_;
    remain_data_ = o.remain_data_;
    state_ = std::move(o.state_);
    executor_ = o.executor_;
    conf_ = std::move(o.conf_);
    buffer_size_ = o.buffer_size_;
    return *this;
  }

  ~urma_socket_t() { close(); }

  bool is_open() const noexcept {
    return state_ != nullptr && !state_->has_close_;
  }

  // Consume data from receive buffer
  std::size_t consume(char* dst, std::size_t sz, int dst_gpu_id) {
    auto len = std::min(sz, remain_data_.size());
    if (len) {
      memcpy(dst, remain_data_.data(), len);
      remain_data_ = remain_data_.substr(len);
      if (remain_data_.empty()) {
        // Return buffer to pool
      }
    }
    return len;
  }

  std::size_t remain_read_buffer_size() { return remain_data_.size(); }

  void set_read_buffer_len(std::size_t has_read_size, std::size_t remain_size) {
    remain_data_ = std::string_view{
        reinterpret_cast<char*>(state_->recv_buf_.addr) + has_read_size,
        remain_size};
  }

  // Get current receive buffer as ibv_sge-compatible structure
  urma_sge get_recv_buffer() {
    assert(remain_read_buffer_size() == 0);
    assert(state_->recv_buf_.addr != nullptr);
    return state_->recv_buf_.subview();
  }

  void post_recv(callback_t&& cb) { state_->post_recv_impl(std::move(cb)); }

  void post_send(urma_sge buffer, callback_t&& cb) {
    state_->post_send_impl(buffer, std::move(cb));
  }

  // For ib_socket_t compatibility - accept ibv_sge and convert
  void post_send(ibv_sge buffer, callback_t&& cb) {
    urma_sge sge;
    sge.addr = buffer.addr;
    sge.length = buffer.length;
    sge.lkey = buffer.lkey;
    post_send(sge, std::move(cb));
  }

  uint32_t get_buffer_size() const noexcept { return buffer_size_; }

  config_t& get_config() noexcept { return conf_; }
  const config_t& get_config() const noexcept { return conf_; }

  async_simple::coro::Lazy<std::error_code> waiting_write_over() {
    return state_->waiting_write_over();
  }

  // Accept incoming URMA connection via TCP handshake
  async_simple::coro::Lazy<std::error_code> accept(
      std::string_view magic = "") noexcept {
    urma_socket_t::urma_socket_info peer_info;
    constexpr auto sz = struct_pack::get_needed_size(peer_info);
    assert(magic.size() < sz.size());

    char buffer[sz.size()];
    memcpy(buffer, magic.data(), magic.size());

    auto [ec, _] = co_await async_read(
        state_->soc_,
        asio::buffer(buffer + magic.size(), sizeof(buffer) - magic.size()));
    if (ec) [[unlikely]] {
      co_return ec;
    }

    auto ec2 = struct_pack::deserialize_to(peer_info, std::span{buffer});
    if (ec2) [[unlikely]] {
      co_return std::make_error_code(std::errc::protocol_error);
    }

    ELOG_DEBUG << "Remote Jetty ID = " << peer_info.jetty_id;
    remote_jetty_id_ = peer_info.jetty_id;

    // Copy remote EID
    std::copy(std::begin(peer_info.eid), std::end(peer_info.eid),
             state_->remote_eid_.begin());

    // Convert EID to address for compatibility
    remote_address_ = eid_to_address(peer_info.eid);
    ELOG_DEBUG << "Remote EID = " << remote_address_;

    buffer_size_ = std::min<uint32_t>(peer_info.buffer_size, conf_.buffer_size);
    ELOG_DEBUG << "Final buffer size = " << buffer_size_;

    // Send back our info
    urma_socket_info local_info{};
    local_info.jetty_id = get_local_jetty_id();
    local_info.buffer_size = conf_.buffer_size;
    get_local_eid(local_info.eid);

    struct_pack::serialize_to((char*)buffer, sz, local_info);
    co_await async_write(state_->soc_, asio::buffer(buffer));

    // Shutdown TCP socket - RDMA is now the data channel
    std::error_code ignore_ec;
    state_->soc_.shutdown(asio::ip::tcp::socket::shutdown_both, ignore_ec);
    state_->soc_.close(ignore_ec);

    co_return std::error_code{};
  }

  void prepare_accept(asio::ip::tcp::socket soc) noexcept {
    state_->soc_ = std::move(soc);
  }

  async_simple::coro::Lazy<std::error_code> accept(
      asio::ip::tcp::socket soc) noexcept {
    state_->soc_ = std::move(soc);
    return accept();
  }

  asio::ip::address get_remote_address() const noexcept {
    return remote_address_;
  }

  uint32_t get_remote_qp_num() const noexcept {
    return remote_jetty_id_;  // jetty_id serves as QP number in URMA
  }

  asio::ip::address get_local_address() const noexcept {
    return local_address_;
  }

  uint32_t get_local_qp_num() const noexcept {
    return get_local_jetty_id();
  }

  // Magic number for protocol detection (similar to ib_md5_first_header)
  constexpr static uint32_t urma_md5_header =
      struct_pack::get_type_code<urma_socket_t::urma_socket_info>();
  constexpr static char urma_md5_first_header =
      struct_pack::get_type_code<urma_socket_t::urma_socket_info>() % 256;

  // Connect to remote URMA endpoint
  async_simple::coro::Lazy<std::error_code> connect_impl() noexcept {
    try {
      urma_socket_t::urma_socket_info peer_info{};
      peer_info.jetty_id = get_local_jetty_id();
      peer_info.buffer_size = conf_.buffer_size;
      get_local_eid(peer_info.eid);

      constexpr auto sz = struct_pack::get_needed_size(peer_info);
      char buffer[sz.size()];
      struct_pack::serialize_to((char*)buffer, sz, peer_info);

      // Send our info
      auto [ec, len] = co_await async_write(state_->soc_,
                                            asio::buffer(buffer));
      if (ec) {
        co_return std::move(ec);
      }

      // Read remote info
      std::tie(ec, len) = co_await async_read(state_->soc_,
                                               asio::buffer(buffer));
      std::error_code ignore_ec;
      state_->soc_.shutdown(asio::ip::tcp::socket::shutdown_both, ignore_ec);
      state_->soc_.close(ignore_ec);

      if (ec) {
        co_return std::move(ec);
      }

      auto ec2 = struct_pack::deserialize_to(peer_info, std::span{buffer});
      if (ec2) [[unlikely]] {
        co_return std::make_error_code(std::errc::protocol_error);
      }

      remote_jetty_id_ = peer_info.jetty_id;
      std::copy(std::begin(peer_info.eid), std::end(peer_info.eid),
                state_->remote_eid_.begin());
      remote_address_ = eid_to_address(peer_info.eid);
      buffer_size_ = std::min<uint32_t>(peer_info.buffer_size, conf_.buffer_size);

    } catch (const std::system_error& err) {
      co_return err.code();
    }
    co_return std::error_code{};
  }

  async_simple::coro::Lazy<std::error_code> connect(
      const std::string& host, const std::string& port) noexcept {
    auto ec = co_await async_connect(get_coro_executor(), state_->soc_,
                                     host, port);
    if (ec) [[unlikely]] {
      co_return std::move(ec);
    }
    ec = co_await connect_impl();
    if (ec) [[unlikely]] {
      close();
    }
    co_return ec;
  }

  template <typename EndPointSeq>
  async_simple::coro::Lazy<std::error_code> connect(
      const EndPointSeq& endpoint) noexcept {
    auto ec = co_await async_connect(state_->soc_, endpoint);
    if (ec) [[unlikely]] {
      co_return std::move(ec);
    }
    ec = co_await connect_impl();
    if (ec) [[unlikely]] {
      close();
    }
    co_return ec;
  }

  void close() {
    if (state_) {
      if (!state_->has_close_.exchange(true)) {
        asio::dispatch(executor_->get_asio_executor(), [state = state_]() {
          state->close(false);
        });
      }
    }
  }

  auto get_executor() const { return executor_->get_asio_executor(); }
  auto get_coro_executor() const { return executor_; }

  urma_buffer_t release_send_buffer() noexcept {
    return state_->release_send_buffer();
  }

  std::size_t sent_request_count() const noexcept {
    return state_->sent_request_count();
  }

  std::optional<urma_sge> get_send_buffer_view() noexcept {
    if (state_->send_queue_.empty()) {
      // Get buffer from pool
      urma_buffer_t buf;
      // TODO: Get from URMA buffer pool
      if (!buf) {
        ELOG_WARN << "buffer out of limit, get send buffer failed";
        close();
        return std::nullopt;
      }
      state_->send_queue_.push(std::move(buf));
    }
    return state_->send_queue_.front().subview(state_->send_buffer_data_size_);
  }

  std::size_t get_free_send_buffer_size() noexcept {
    return buffer_size_ - state_->send_buffer_data_size_;
  }

  void consume_send_buffer(std::size_t sz) noexcept {
    state_->send_buffer_data_size_ += sz;
  }

  std::shared_ptr<detail::urma_socket_shared_state_t> get_state() const noexcept {
    return state_;
  }

  detail::urma_socket_shared_state_t* get_raw_state() const noexcept {
    return state_.get();
  }

  // URMA-specific methods
  uint32_t get_local_jetty_id() const {
    if (state_ && state_->jetty_) {
      return state_->jetty_->jetty_id.id;
    }
    return 0;
  }

  void get_local_eid(uint8_t* eid) const {
    if (state_ && state_->jfc_) {
      std::copy(std::begin(state_->jfc_->jfc_id.eid.raw),
                std::end(state_->jfc_->jfc_id.eid.raw), eid);
    }
  }

 private:
  void init(const config_t& config) {
    conf_ = config;
    conf_.recv_buffer_cnt = std::max<uint16_t>(conf_.recv_buffer_cnt, 1);
    conf_.send_buffer_cnt = std::max<uint16_t>(conf_.send_buffer_cnt, 1);

    ELOG_INFO << "urma_socket config: recv_buffer_cnt:" << conf_.recv_buffer_cnt
             << ", send_buffer_cnt:" << conf_.send_buffer_cnt
             << ", buffer_size:" << conf_.buffer_size;

    state_ = std::make_shared<detail::urma_socket_shared_state_t>(
        executor_, conf_.recv_buffer_cnt, conf_.send_buffer_cnt,
        conf_.recv_buffer_cnt + 2, conf_.buffer_size);

    buffer_size_ = conf_.buffer_size;
  }

  // Convert URMA EID to asio::ip::address for compatibility
  static asio::ip::address eid_to_address(const uint8_t* eid) {
    // EID is 16 bytes, we can format first 4 bytes as IPv4 for simplicity
    // or use a proper conversion
    char buf[64];
    snprintf(buf, sizeof(buf), "%d.%d.%d.%d",
             eid[0], eid[1], eid[2], eid[3]);
    std::error_code ec;
    auto addr = asio::ip::make_address(buf, ec);
    if (ec) {
      // Fallback to localhost if conversion fails
      return asio::ip::make_address_v4(0x7F000001);  // 127.0.0.1
    }
    return addr;
  }

  asio::ip::address remote_address_;
  uint32_t remote_jetty_id_{0};
  std::string_view remain_data_;
  std::shared_ptr<detail::urma_socket_shared_state_t> state_;
  coro_io::ExecutorWrapper<>* executor_;
  config_t conf_;
  uint32_t buffer_size_{0};
  asio::ip::address local_address_;
};

}  // namespace coro_io