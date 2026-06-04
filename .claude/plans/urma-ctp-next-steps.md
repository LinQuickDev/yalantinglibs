# URMA CTP 实现计划 - 下一步

## 当前状态

### 已完成 ✅
1. `urma_socket.hpp` 重构修复:
   - `urma_query_jetty` 调用已移除，直接通过 `jetty_->jetty_id.id` 访问
   - `urma_import_jetty` 参数已正确初始化 (`trans_mode`, `type`, `tp_type`, `flag`)
   - ASIO 协程兼容性问题已修复 (`async_simple::CurrentExecutor{}`)
   - 添加缺失方法: `prepare_accept`, `get_remote_address`, `get_local_address`, `get_remote_qp_num`, `get_local_qp_num`
   - 添加 `urma_md5_header`, `urma_md5_first_header` 常量

### 待解决问题 🔴

#### 1. urma_example.cpp 集成问题

**错误1**: `get_global_urma_device()` 不接受参数
```cpp
// 当前错误用法:
coro_io::get_global_urma_device({
    .dev_name = "",
    .buffer_pool_config = {...},
    .eid_index = 0
});
```

**错误2**: `coro_rpc_client::config::socket_config` 是 variant 类型，不支持 URMA
```cpp
// 当前错误用法:
conf.socket_config = urma_config;  // urma_socket_t::config_t 不能直接赋值
```

#### 2. 缺失的 API
- `urma_device_wrapper_t::init(const urma_config&)` - 需要支持完整配置
- `coro_rpc_client::config` variant 需要 URMA 类型支持

---

## 参考实现: ib_socket 集成模式

### ib_socket 的结构

```cpp
// ib_socket.hpp
class ib_socket_t {
 public:
  struct config_t {
    uint32_t cq_size = 128;
    uint16_t recv_buffer_cnt = 8;
    uint16_t send_buffer_cnt = 4;
    ibv_qp_type qp_type = IBV_QPT_RC;
    ibv_qp_cap cap = {...};
    std::shared_ptr<ib_device_t> device;
  };

  struct ib_socket_info {
    uint8_t gid[16];
    uint16_t lid;
    uint32_t buffer_size;
    uint32_t qp_num;
  };

  ib_socket_t(coro_io::ExecutorWrapper<>* executor, const config_t& config);
  async_simple::coro::Lazy<std::error_code> connect(const std::string& addr, const std::string& port);
  async_simple::coro::Lazy<std::error_code> accept() noexcept;
  void prepare_accpet(asio::ip::tcp::socket soc) noexcept;
  // ... 其他方法
};
```

### socket_wrapper 的 variant 支持

```cpp
// socket_wrapper.hpp
class socket_wrapper_t {
#ifdef YLT_ENABLE_IBV
  std::unique_ptr<ib_socket_t> ib_socket_;
#endif
#ifdef YLT_ENABLE_URMA
  std::unique_ptr<urma_socket_t> urma_socket_;
#endif

 public:
  template<typename T>
  auto visit(T&& op) {
#ifdef YLT_ENABLE_IBV
    if (ib_socket_) return op(*ib_socket_);
#endif
#ifdef YLT_ENABLE_URMA
    if (urma_socket_) return op(*urma_socket_);
#endif
    return op(*socket_);
  }
};
```

---

## 下一步修改计划

### 阶段1: 修复 urma_example.cpp

#### 1.1 修改 get_global_urma_device() 支持配置参数

参考 ib_device 的初始化模式:

```cpp
// urma_device.hpp
inline bool urma_device_wrapper_t::init(const urma_init_config_t& config) {
  // 支持配置参数初始化
}
```

#### 1.2 修改 coro_rpc_client::config 支持 URMA

需要在 variant 中添加 URMA 类型:

```cpp
// coro_rpc_client.hpp
struct config_t {
  using socket_config_t = std::variant<
      tcp_config,
      tcp_with_ssl_config,
      tcp_with_ntiles_config
#ifdef YLT_ENABLE_URMA
      , urma_socket_t::config_t  // 添加 URMA 支持
#endif
  >;
  socket_config_t socket_config;
};
```

### 阶段2: 实现 URMA 连接的建立流程

#### 2.1 参考 ib_socket 的连接建立

```cpp
// ib_socket 连接流程:
async_simple::coro::Lazy<std::error_code> connect(const std::string& addr, const std::string& port) {
  // 1. TCP 连接
  // 2. 交换 socket_info (gid, lid, buffer_size, qp_num)
  // 3. 初始化 QP
  // 4. 返回
}
```

#### 2.2 URMA CTP 模式连接

根据 URMA API Guide，CTP 模式流程:
1. 创建 JFC (Completion Channel)
2. 创建 JFR (Receive Queue)
3. 创建 Jetty (Combined send/recv)
4. 通过 TCP 交换 Jetty ID 和 EID
5. 导入远端 Jetty
6. 建立连接

### 阶段3: 测试验证

1. 单元测试: urma_socket 基本功能
2. 集成测试: urma_example 与 coro_rpc 集成

---

## 参考文档

- URMA API Guide: `.claude/skills/query-urma-docs/URMA API Guide.ch.md`
- URMA User Guide: `.claude/skills/query-urma-docs/URMA User Guide.ch.md`
- ib_socket 实现: `include/ylt/coro_io/ibverbs/ib_socket.hpp`
- socket_wrapper: `include/ylt/coro_io/socket_wrapper.hpp`
