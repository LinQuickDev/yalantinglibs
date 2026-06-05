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
#ifndef YLT_CORO_IO_DETAIL_CIRCLE_BUFFER_HPP
#define YLT_CORO_IO_DETAIL_CIRCLE_BUFFER_HPP

#include <cassert>
#include <cstdint>
#include <vector>

namespace coro_io {
namespace detail {

template <typename T>
struct circle_buffer {
  std::vector<T> queue;
  uint32_t front_ = 0, end_ = 0;
  bool may_empty = true;
  circle_buffer() = default;
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

}  // namespace detail
}  // namespace coro_io

#endif  // YLT_CORO_IO_DETAIL_CIRCLE_BUFFER_HPP