# yaLanTingLibs 项目结构与能力

> 本文档面向后续开发需求,对 yaLanTingLibs 仓库的代码结构、各模块能力、构建/测试/CI 流程、依赖生态,以及当前 `supercache_dev` 分支重点推进的 **URMA 传输子系统(华为灵衢 UB,替换 TCP)** 进行系统性梳理。
>
> 所有信息均来自代码与文件的实际探查,标注了 `文件路径:行号` 以便跳转。版本基线:**v0.6.0**,分支:**`supercache_dev`**。

---

## 0. 速查总览

| 项 | 内容 |
|---|---|
| 项目 | yaLanTingLibs(阿里出品的现代 C++ 高性能工具库集合) |
| 版本 | 0.6.0(`include/ylt/version.hpp` 中 `YLT_VERSION 600`) |
| 许可证 | Apache License 2.0(`LICENSE` / `NOTICE`) |
| 库形态 | **纯头文件库**(CMake 中为 INTERFACE target `yalantinglibs::yalantinglibs`) |
| 语言标准 | C++17(仅序列化子集)/ C++20(全功能,含协程) |
| 构建系统 | CMake(主)+ Bazel(辅,Bzlmod) |
| 安装 | vcpkg / Homebrew / `find_package` / FetchContent / add_subdirectory |
| 模块数 | 12 个稳定模块 + **urma**(新增) |
| 当前分支主题 | 为 coro_rpc 接入 **URMA(华为灵衢 UB 传输)**,替换 TCP 降时延、提吞吐 |

**模块清单:**

| 类别 | 模块 | 一句话能力 |
|---|---|---|
| 序列化 | **struct_pack** | 编译期反射二进制序列化,号称比 protobuf 快 2~20× |
| 序列化 | **struct_pb** | protobuf 风格序列化 + `.proto`→struct 代码生成器 |
| 序列化 | **struct_json / struct_xml / struct_yaml** | 反射式 struct↔JSON/XML/YAML(基于 iguana) |
| RPC | **coro_rpc** | C++20 协程 RPC,>0.4M QPS/线程 |
| I/O | **coro_io** | 协程 I/O 工具(连接池、负载均衡、限流、文件、定时器、channel) |
| I/O | **coro_http** | 协程 HTTP(S) client+server(基于 cinatra) |
| 可观测 | **easylog** | 轻量日志(C++17 兼容) |
| 可观测 | **metric** | counter/gauge/histogram/summary/dynamic_metric/system_metric |
| 基础设施 | **reflection** | 编译期静态反射原语(被上述各库复用) |
| 基础设施 | **util** | 并发队列、expected、dragonbox、sharded map、随机数、时间等工具集 |
| 传输(新) | **urma** | 华为 **灵衢 UB** 传输协议(URMA)绑定 + C++ 协程封装,替换 TCP 降低 RPC 时延、提升吞吐 |

---

## 1. 项目定位与背景

yaLanTingLibs 是一组"易用 + 高性能"的现代 C++ 库,目标是让开发者快速构建高性能应用。整个项目以**头文件**形式分发(`include/ylt/` 下无任何 `.cpp`),仅 examples / tests / benchmark / struct_pb 代码生成器在 `src/` 下有编译单元。

设计哲学:
- **头文件优先**:复制 `include/ylt/` 即可使用,无链接步骤。
- **编译期反射**:struct_pack / struct_json 等依赖 `reflection/` 提供的编译期成员遍历,实现"一行序列化"。
- **C++20 协程**:coro_rpc / coro_io / coro_http 基于 vendored 的 **async_simple** 协程运行时与 **asio**。
- **零拷贝 / 内存语义**:URMA(灵衢 UB)/ IBV 传输路径支持 `owned_data_view` 分段零拷贝。

**当前开发主线(`supercache_dev` 分支):** 为 coro_io 与 coro_rpc 接入华为 **灵衢总线(UB,Unified Bus)** 传输 —— 即 **URMA(Unified Remote Memory Access)**。其目的是**替换 coro_rpc 底层的 TCP 传输**,通过内存语义通信**显著降低 RPC 端到端时延、提升吞吐**。

> URMA 是华为 **UMDK(Unified Memory Development Kit,灵衢内存语义开发包)** 的核心通信库,屏蔽底层驱动差异,提供单边/双边/原子等远端内存访问。相关概念:UB(灵衢统一总线)、UBVA(统一虚拟地址)、Segment、Jetty(JFS/JFR/JFC)。详见 `.claude/skills/query-urma-docs/URMA User Guide.ch.md`。同族组件还包括 URPC、ULOCK、**USOCK(兼容标准 Socket 接口,使能 TCP 应用零修改提升性能)** —— 印证了"替换 TCP 提性能"的定位。

近期提交:

- `87d961d add performance metric` — 新增 URMA RPC 18 阶段延迟剖析(`urma_benchmark_profile.hpp`)
- `3d890b6 docs: add URMA RPC usage HTML` — `website/docs/public/urma_rpc_usage.html`
- `38907a6` / `568c1ac` / `92fad43` — `urma_buffer.hpp`、`urma_io.hpp`、`urma_socket.hpp`、coro_rpc 连接层与协议层集成、`urma_benchmark` 示例

相关远程分支还有 `fix_rdma2`、`gdr_support`、`gdr_support2`(GPUDirect RDMA 方向)。

---

## 2. 目录结构总览

```
yalantinglibs/
├── include/                      ← 所有库头文件(库本体)
│   └── ylt/
│       ├── coro_rpc/             (facade 头 + impl/)
│       ├── coro_io/              (含 urma/ 子目录)
│       ├── coro_http/
│       ├── struct_pack/  struct_pb/  struct_json/  struct_xml/  struct_yaml/
│       ├── easylog/  metric/  reflection/  util/
│       ├── urma/                 ← URMA C API 绑定(新增)
│       ├── thirdparty/           ← vendored: asio / async_simple / frozen
│       └── standalone/           ← vendored: cinatra / iguana
├── src/                          ← 每个模块的 examples / tests / benchmark
│   ├── coro_rpc/  coro_io/  coro_http/  struct_pack/  ...
│   │   ├── examples/
│   │   ├── tests/
│   │   └── benchmark/
│   └── include/doctest.h         ← 测试框架(doctest,CC0)
├── cmake/                        ← CMake 模块:build/config/develop/install/subdir/utils.cmake + Find/
├── bazel/  BUILD.bazel  MODULE.bazel  WORKSPACE(.bzlmod)  .bazelrc
├── website/                      ← VitePress 文档站(中/英 + Doxygen)
├── .github/workflows/            ← 14 个 CI 流水线
├── .claude/                      ← Agent/工具配置(skills/、rules/、plans/)
├── scripts/                      ← run_ntls_e2e_tests.sh
├── CMakeLists.txt                ← 根构建入口
├── README.md  LICENSE  NOTICE  vcpkg.json  coverage_gen.sh
└── 项目结构与能力.md             ← 本文档
```

**关键约定:**

1. **库代码全部在 `include/ylt/<模块>/`**。`src/<模块>/` 只放 examples / tests / benchmark,不参与库本体安装。
2. **Facade 头**:对外暴露的聚合头(如 `coro_rpc/coro_rpc_server.hpp`、`coro_rpc/coro_rpc_client.hpp`)通常很短,真正实现在 `impl/` 下。例如 `coro_rpc_server.hpp:17` 仅 `#include "impl/coro_rpc_server.hpp"`。
3. **每个模块自治**:`src/<模块>/examples/CMakeLists.txt` 会判断自身是否为顶层项目,否则用 `find_package(yalantinglibs REQUIRED)`。
4. **vendored 依赖** 不在 `src/` 而在 `include/ylt/thirdparty/` 与 `include/ylt/standalone/`,随库一起安装。

---

## 3. 模块能力详解

### 3.1 struct_pack — 编译期反射二进制序列化

- **定位**:零依赖、编译期反射的二进制序列化,1 行代码完成序列化/反序列化,性能优于 protobuf(官方称 2~20×)。是 coro_rpc 的默认序列化层。
- **关键头**:`include/ylt/struct_pack.hpp` + `include/ylt/struct_pack/`。
- **核心 API**:`struct_pack::serialize(obj)` / `struct_pack::deserialize<T>(buffer)`;支持派生类(`derived_class`)、兼容字段、对齐、流式、跨平台、用户自定义序列化。
- **特性**:编译期类型布局、`#pragma pack` 支持、fast_varint、可选 `YLT_ENABLE_STRUCT_PACK_OPTIMIZE`(更优代码,但增加编译时间)与 `YLT_ENABLE_STRUCT_PACK_UNPORTABLE_TYPE`(如 wchar_t)。
- **依赖**:`reflection/`、`util/`。

### 3.2 struct_pb — protobuf 风格序列化

- **定位**:类似 protobuf 的序列化,提供 `.proto` → C++ struct 的代码生成器。
- **关键头**:`include/ylt/struct_pb.hpp` + `struct_pb/struct_pb_impl.hpp`。
- **代码生成器**:`src/struct_pb/tools/proto_to_struct.cpp`。

### 3.3 struct_json / struct_xml / struct_yaml — 反射式结构互转

- **定位**:基于反射,一行代码完成 struct ↔ JSON / XML / YAML。
- **底层**:vendored 的 **iguana**(`include/ylt/standalone/iguana/`)。
- **关键头**:`include/ylt/struct_json/`、`struct_xml/`、`struct_yaml/`。

### 3.4 coro_rpc — C++20 协程 RPC 框架 ⭐

- **定位**:高性能协程 RPC,官方称 >0.4M QPS/线程;支持 SSL / NTLS / **RDMA(IBV / URMA)** / GPUDirect RDMA。
- **关键头**(facade):
  - `include/ylt/coro_rpc/coro_rpc_server.hpp`(→ `impl/coro_rpc_server.hpp`)
  - `include/ylt/coro_rpc/coro_rpc_client.hpp`(→ `impl/coro_rpc_client.hpp`)
  - `include/ylt/coro_rpc/coro_rpc_context.hpp`(→ `impl/protocol/coro_rpc_protocol.hpp`,真实 context 在 `impl/context.hpp`)
- **Server 核心 API**(`impl/coro_rpc_server.hpp`):
  ```cpp
  // 构造(thread_num, port, address, conn_timeout, tcp_no_delay)  L83
  coro_rpc_server_base(size_t thread_num = hardware_concurrency(),
                       unsigned short port = 9001,
                       std::string address = "0.0.0.0", ...);
  [[nodiscard]] coro_rpc::err_code start() noexcept;            // L186 阻塞
  async_simple::Future<coro_rpc::err_code> async_start();       // L203
  void stop();                                                   // L289
  // 注册处理器(模板非类型参数 = 函数地址)
  template<auto... functions> void register_handler();          // L420 自由函数
  template<auto func> void register_handler(const auto& key);   // L425 带名
  template<auto first, auto... functions>
  void register_handler(util::class_type_t<decltype(first)>* self);  // L385 成员函数
  ```
  对外类型为别名 `coro_rpc_server = coro_rpc_server_base<config_t>`(`impl/default_config/coro_rpc_config.hpp:62`)。路由器类 `router` 在 `impl/router.hpp:51`,自带同名 `register_handler` 重载。
- **Client 核心 API**(`impl/coro_rpc_client.hpp`):
  ```cpp
  coro_rpc_client(coro_io::ExecutorWrapper<>* executor = coro_io::get_global_executor(),
                  config conf = {});                          // L278
  [[nodiscard]] async_simple::coro::Lazy<coro_rpc::err_code>
      connect(std::string host, std::string port, ...);        // L596 等 4 个重载
  template<auto func, typename... Args>
  [[nodiscard]] async_simple::coro::Lazy<rpc_result<...>>
      call(Args&&... args);                                    // L851 主调用(返回 Lazy,本身就是异步)
  template<auto func, typename... Args>
  auto call_for(auto request_timeout_duration, Args&&... args);// L873 带超时
  ```
  > 注意:无独立 `async_call()` —— `call()`/`call_for()` 直接返回 `Lazy`,即异步 API。`sync_call`/`sync_connect` 仅在 `#ifdef UNIT_TEST_INJECT` 下编译,不属于发布 API。
- **Handler Context**:处理器首参收到 `context_base<return_msg_type, rpc_protocol>`(`impl/context.hpp:42`),提供:
  - `response_msg(args...)`(`L108`)序列化并发送响应;
  - `response_error(err_code[, msg])`(`L81`/`L93`);
  - `get_context_info()`(`L143`)获取连接 / 请求 ID / 头 / attachment buffer。
- **协议层**:`impl/protocol/coro_rpc_protocol.hpp`;`CORO_RPC_USE_OTHER_RPC=ON` 时支持自定义协议(见 `examples/user_defined_rpc_protocol/rest_rpc/`)。
- **序列化**:默认 struct_pack;支持 attachment / `owned_data_view` 分段零拷贝(URMA fast path)。
- **依赖**:`coro_io`、`struct_pack`、`easylog`、`reflection`、`util`、vendored asio/async_simple。

### 3.5 coro_io — 协程 I/O 工具集

| 头文件(均在 `include/ylt/coro_io/`) | 主类型 | 能力 |
|---|---|---|
| `coro_io.hpp` | `callback_awaitor`(L154)、`period_timer`(L616)、`high_resolution_timer`(L633)、`channel<R>`(L679)、`endpoint`(L840)、`ExecutorWrapper`(L54) | 协程 I/O 原语:回调桥接、定时器、channel、执行器封装 |
| `io_context_pool.hpp` | `io_context_pool`(L188) | 多线程 io_context 池,round-robin 选 executor |
| `client_pool.hpp` | `client_pool<client_t, ...>`(L67) | 异步 client 连接/实例池,空闲回收 |
| `load_balancer.hpp` | `load_balancer<client_t, ...>`(L34)+`enum load_balance_algorithm{RR,WRR,random}`(L28) | 多端点负载均衡 |
| `rate_limiter.hpp` | `rate_limiter`(L29)、`abstract_smooth_rate_limiter`(L71) | 协程令牌桶限流,`acquire(permits)` / `set_rate(...)` |
| `coro_file.hpp` | `coro_file`(L401,顺序)、`random_coro_file`(L702,随机) | 协程文件 I/O |
| `server_acceptor.hpp` | `tcp_server_acceptor`(L77)、`server_acceptor_base`(L29) | 监听 / accept 抽象 |
| `socket_wrapper.hpp` | `socket_wrapper_t`(L29) | **类型擦除的 socket**,统一 TCP / SSL / IBV / URMA |
| `data_view.hpp` | `data_view`(L29)、`owned_data_view`(L74) | 带 `gpu_id()` 的内存视图(-1 = CPU) |
| `memory_owner.hpp` | `memory_owner_t`(L25) | CPU / CUDA 内存 RAII 持有 |
| `heterogeneous_buffer.hpp` | `heterogeneous_buffer`(L29) | `variant<string, memory_owner_t>`,异构缓冲 |

> `coro_io::channel<R>` 位于 `coro_io.hpp:679`(不存在独立的 `coro_channel.hpp`)。

### 3.6 coro_http — 协程 HTTP(S)

- **定位**:C++20 协程 HTTP 客户端 + 服务端,支持 WebSocket、multipart 上传、chunked / ranges 下载。
- **底层**:vendored 的 **cinatra**(`include/ylt/standalone/cinatra/`)。
- **关键头**:`coro_http/coro_http_client.hpp`、`coro_http_server.hpp`。支持 SSL(`YLT_ENABLE_SSL`)与 NTLS/铜锁(`YLT_ENABLE_NTLS`)。

### 3.7 easylog — 轻量日志

- **定位**:无依赖、C++17 兼容的轻量日志库。
- **关键头**:`include/ylt/easylog.hpp` + `easylog/record.hpp`、`appender.hpp`。

### 3.8 metric — 指标框架

- **定位**:Prometheus 风格的指标(counter / gauge / histogram / summary),含动态(label 维度)指标与系统指标。
- **关键头**(`include/ylt/metric/`):
  - `metric.hpp` — `metric_t`(L42)、`static_metric`(L201)、`MetricType`(L27)
  - `counter.hpp` — `counter_t`/`counter_d` + 动态 `dynamic_counter_Nt/d`(1~5 维)
  - `gauge.hpp` — `gauge_t`/`gauge_d` + 动态版
  - `histogram.hpp` — `histogram_t`/`histogram_d` + 动态版
  - `summary.hpp` — `summary_t` + 动态版(基于 `summary_impl.hpp` 分位数估计)
  - `metric_manager.hpp` — `static_metric_manager` / `dynamic_metric_manager` / `system_metric_manager`
  - `system_metric.hpp` — `ProcStat`(L317)、`ProcIO`(L210),来自 `/proc`

### 3.9 reflection — 编译期静态反射原语

被 struct_pack / struct_json 等复用。`include/ylt/reflection/`:

| 文件 | 关键 API |
|---|---|
| `member_count.hpp` | `members_count<T>()`(L184)、`members_count_v<T>` |
| `member_names.hpp` | `get_member_names<T>()`、`get_struct_name<T>()`、`name_of<T>(index)`、`index_of<T>(name)`、`for_each<T>(func)`、`member_names_map<T>`、`member_offsets<T>` |
| `member_ptr.hpp` | `visit_members(t, visitor)`(L123)、`object_to_tuple(t)`、`struct_to_tuple<T>()`、`tuple_view(t)` |
| `member_value.hpp` | `get<T>(index/name)`、`for_each(t, func)`、`set_member_ptr`、`operator""_ylts` 字面量 |
| `private_visitor.hpp` | `private_visitor<T, field...>`(L8) |
| `template_string.hpp` | `type_string<T>()`、`enum_string<V>()`、`field_string<field>()`、`get_enum_map<...>()` |
| `template_switch.hpp` | `template_switch(index, args...)`(L16)运行时索引→编译期分发 |
| `user_reflect_macro.hpp` | 宏 `YLT_REFL(STRUCT, ...)`(L27)、`YLT_REFL_PRIVATE(...)`(L112)、`visit_private_fields(...)` |

### 3.10 util — 工具集

`include/ylt/util/`:

| 文件 | 关键内容 |
|---|---|
| `expected.hpp` / `tl/expected.hpp` | `expected<T,E>`(优先 `std::expected`,否则 `tl::expected`) |
| `type_traits.h` | `function_traits<F>`(L35)、`function_return_type_t`、`class_type_t`、`is_specialization_v`、`is_invocable_v` |
| `utils.hpp` | `coro_rpc::func_id<func>()`(L23,MD5 函数 ID) |
| `function_name.h` | `get_func_name<func>()`(L33,剥离 MSVC 调用约定) |
| `meta_string.hpp` | `meta_string<N>`(L36)编译期字符串 |
| `random.hpp` | `random_engine<>()` |
| `string_finder.hpp` | `find_mode_type{any_of,full_match,...}` |
| `time_util.h` | `get_timestamp`、`faster_mktime`、`get_local_time_str`、`get_gmt_time_str`、`is_leap` |
| `ff_ptr.hpp` | `ff_ptr`(L229,前向指针)、`ff_shared_ptr`、`ff_unique_ptr` |
| `map_sharded.hpp` | `map_lock_t`、`map_sharded_t`(分片加锁 map) |
| `magic_names.hpp` | `parse_qualified_function_name`、`qualified_name_of`、`name_of` |
| `atomic_shared_ptr.hpp` | `atomic_shared_ptr`(L281/L596) |
| `concurrentqueue.h` | vendored moodycamel 无锁 MPMC 队列 |
| `dragonbox.h` / `dragonbox_to_chars.h` | vendored Dragonbox 浮点→十进制算法 |
| `b_stacktrace.h` | vendored 栈回溯捕获 |

---

## 4. ⭐ URMA 传输子系统(替换 TCP,当前开发重点)

### 4.1 背景与定位

- **URMA**(**Unified Remote Memory Access**,统一远程内存访问)是华为 **UMDK(Unified Memory Development Kit,灵衢内存语义开发包)** 的核心通信库,构建在 **灵衢统一总线(UB,Unified Bus)** 之上。它屏蔽底层硬件驱动差异,提供单边 / 双边 / 原子等远端内存访问,核心抽象为 **Jetty(JFS / JFR / JFC)、Segment、UBVA、EID**。
- **在本项目中的定位**:**作为 coro_rpc 的底层传输,替换 TCP**,借助内存语义通信**显著降低 RPC 端到端时延、提升吞吐**。URMA 默认承载于 **UBoE(UB over Ethernet)**,也可跑在灵衢原生总线硬件上。
- 项目同时维护多条传输路径,通过 `coro_io::socket_wrapper_t` 的类型擦除统一接入 coro_rpc:
  - **URMA**(`YLT_ENABLE_URMA`)—— **当前 `supercache_dev` 主线**,基于华为灵衢 UB,替换 TCP 以降时延/提吞吐;
  - **IBV(libibverbs)**(`YLT_ENABLE_IBV`)—— 历史 InfiniBand/RoCE 路径,支持 GPUDirect RDMA(GDR,需 `YLT_ENABLE_CUDA`);
  - **TCP / SSL / NTLS** —— 默认 asio 路径。
- URMA 传输类型默认为 **CTP**(见 `urma_benchmark/README.md`)。
- **与"RDMA"的关系**:URMA 在抽象层面与 RDMA verbs 类似(远端内存语义、单边/双边操作),但底层是华为灵衢 UB 总线而非传统 InfiniBand/RoCE。文档/代码中出现的 "rdma"、"ibv" 字样多为历史命名或泛指,需注意区分。

### 4.2 分层结构

```
┌─────────────────────────────────────────────────────────┐
│  coro_rpc  (coro_connection.hpp / coro_rpc_protocol.hpp │
│             / coro_rpc_client.hpp)                       │
│   条件编译 #if YLT_ENABLE_URMA → URMA fast path          │
│   - read_header / read_payload / dispatch / response     │
│   - owned_data_view 分段零拷贝(attach_sink)             │
└────────────────────────┬────────────────────────────────┘
                         │ 调用
┌────────────────────────▼────────────────────────────────┐
│  coro_io/urma/   (C++ 协程封装)                          │
│  ├ urma_io.hpp           async_urma_read / read_views /  │
│                          write completion, profile 插桩  │
│  ├ urma_socket.hpp       urma_socket_t(Jetty/JFS/JFR    │
│                          封装, post_recv, recv_buffer)  │
│  ├ urma_buffer.hpp       urma_buffer_t / urma_buffer_   │
│                          pool_t(整段内存一次注册)       │
│  ├ urma_device.hpp       urma_device_wrapper_t /        │
│                          urma_device_manager             │
│  └ urma_benchmark_       enum class stage(18 阶段) +    │
│     profile.hpp          thread_samples / record_since  │
└────────────────────────┬────────────────────────────────┘
                         │ 调用
┌────────────────────────▼────────────────────────────────┐
│  urma/  (C API 绑定, 来自 liburma)                       │
│  ├ urma_api.h       urma_init / urma_create_jetty /     │
│  │                  urma_register_seg / urma_post_* ... │
│  ├ urma_types.h     urma_jfc_t / urma_jfr_t /           │
│  │                  urma_jetty_t / urma_seg_t / EID ... │
│  ├ urma_provider.h  urma_provider_ops_t / urma_ops_t    │
│  ├ urma_opcode.h / urma_cmd.h / urma_perf.h /           │
│  └ urma_types_str.h                                      │
└─────────────────────────────────────────────────────────┘
```

### 4.3 C API 层(`include/ylt/urma/`)

`urma_api.h` 暴露的主要函数族(按功能分组):

| 类别 | 代表函数 |
|---|---|
| 生命周期 / 设备 / 上下文 | `urma_init` `urma_uninit` `urma_get_device_list` `urma_get_device_by_name` `urma_create_context` `urma_delete_context` |
| JFC(完成队列) | `urma_create_jfc` `urma_poll_jfc` `urma_rearm_jfc` `urma_wait_jfc` `urma_ack_jfc` `urma_delete_jfc` |
| JFS(发送队列) | `urma_create_jfs` `urma_active_jfs` `urma_deactive_jfs` `urma_post_jfs_wr` |
| JFR(接收队列) | `urma_create_jfr` `urma_import_jfr` `urma_post_jfr_wr` `urma_advise_jfr` |
| Jetty(收发端点) | `urma_create_jetty` `urma_bind_jetty` `urma_import_jetty` `urma_post_jetty_send_wr` `urma_post_jetty_recv_wr` |
| 内存段 | `urma_register_seg` `urma_unregister_seg` `urma_import_seg` |
| 数据传输 | `urma_write` `urma_read` `urma_send` `urma_recv` |
| Token / 地址 | `urma_alloc_token_id` `urma_get_eid_by_ip` `urma_get_net_addr_list` |

主要不透明句柄类型(定义于 `urma_types.h`):`urma_device_t`(L371)、`urma_context_t`(L394)、`urma_jfc_t`(L491)、`urma_jfs_t`(L577)、`urma_jfr_t`(L673)、`urma_jetty_t`(L805)、`urma_target_jetty_t`(L775)、`urma_seg_t`(L969)、`urma_eid_t`(L147)、`urma_token_id_t`(L946)等。

> 文档可参考 `.claude/skills/query-urma-docs/`(三份中文文档:API Guide / User Guide / QuickStart),以及 `website/docs/public/urma_rpc_usage.html`。

### 4.4 C++ 封装层(`include/ylt/coro_io/urma/`)

| 文件 | 主要类型 | 职责 |
|---|---|---|
| `urma_socket.hpp` | `urma_socket_t`(L464)、`urma_deleter`(L69)、`urma_socket_shared_state_t`(L84)、`urma_recv_buffer_owner`(L52) | 协程化 socket:封装 Jetty/JFS/JFR 生命周期、`post_recv`、接收缓冲管理、`rema_read_buffer_size`/`consume`/`get_recv_buffer`/`set_read_buffer_len` |
| `urma_buffer.hpp` | `urma_buffer_t`(L45)、`urma_buffer_pool_t`(L55) | **整段大内存一次性 `urma_register_seg` 为 segment,再切分成固定大小 buffer**(避免逐个 4KB 注册),`return_buffer` 回收 |
| `urma_io.hpp` | `urma_write_completion_state`(L41)、`async_urma_read`(L67)、`async_urma_read_views`(L113) | async read / 零拷贝 read_view / write 完成队列;各阶段插入 `urma_benchmark_profile` 插桩 |
| `urma_device.hpp` | `urma_device_wrapper_t`(L38)、`urma_device_manager`(L86)、`urma_init_config_t`(L110)、`urma_buffer_pool_config_t`(L103) | 设备枚举 / 初始化 / buffer 池配置 |
| `urma_benchmark_profile.hpp` | `enum class stage`(L32)、`thread_samples`(L104) | 见 4.6 |

> ⚠️ 注意 `urma_device.hpp:83` 有 `using urma_device_t = urma_device_wrapper_t`,会**遮蔽** C 层 `urma_types.h:371` 的 `urma_device_t`。在该 C++ 头可见范围内,`urma_device_t` 指向封装类。

### 4.5 与 coro_rpc 的集成点(替换 TCP 传输)

URMA 作为底层传输替换 TCP,接入点遍布 coro_rpc 的连接/协议/客户端各层(均由 `YLT_ENABLE_URMA` 条件编译):

- **连接层** `include/ylt/coro_rpc/impl/coro_connection.hpp`:服务端 `read_header` / `read_payload` / `dispatch` / `response_queue` / `send_response` 各阶段在 URMA 路径下走 `async_urma_read` 并埋点。
- **协议层** `coro_rpc/impl/protocol/coro_rpc_protocol.hpp`:URMA 条件路径处理 attachment / 分段 view。
- **客户端** `coro_rpc_client.hpp`:`prepare_request` / `send_request` / `recv_header` / `recv_payload`。
- **零拷贝 fast path**(`--rpc attach_sink`):请求载荷以 attachment 发送,服务端 handler 直接读取分段 `owned_data_view`,**不拷贝**成连续 `std::string`。这是 URMA RPC 降时延/提吞吐的核心路径。
- **统一接入**:`coro_io::socket_wrapper_t` 类型擦除,使上层 coro_rpc 代码无需感知 TCP / SSL / IBV / URMA 差异。

### 4.6 URMA 基准与性能剖析

**工具**:`src/coro_rpc/examples/urma_benchmark/`(目标 `coro_rpc_urma_benchmark`,需 `-DYLT_ENABLE_URMA=ON -DBUILD_EXAMPLES=ON`,产物 `build/output/examples/coro_rpc/coro_rpc_urma_benchmark`)。

**两种传输模式**:`--transport {rpc|raw}` —— `rpc` 走完整 coro_rpc + struct_pack;`raw` 绕过 coro_rpc 直发不回包,测纯 ingress 吞吐。

**三种 RPC 语义**:`--rpc {echo|sink|attach_sink}`:
- `echo` 回显载荷;`sink` 仅返回大小(剔除大响应);`attach_sink` 走分段零拷贝 fast path。

**关键参数**:

| 参数 | 默认 | 说明 |
|---|---|---|
| `--device` | `bonding_dev_0` | URMA 设备名 |
| `--eid-index` | 0 | EID 索引 |
| `--buffer-size` | 4096 | URMA SEND chunk(CTP 4KB 上限) |
| `--queue-depth` | 64 | send/recv 队列深度 |
| `--max-memory-mib` | 256 | buffer 池内存上限(按 connections/queue-depth/payload 自动上调) |
| `--connections` | — | RPC 连接数(吞吐模式下每连接一个 worker) |
| `--pipeline-depth` | 1 | 每连接在途 RPC 数(吞吐模式) |
| `--mode` | — | `latency` / `throughput` / `both` |
| `--profile` | off | 启用阶段延迟剖析 |
| `--profile-sample-rate` | 1 | 每 N 个事件采样一次 |

**18 阶段延迟剖析**(`urma_benchmark_profile.hpp`,`enum class stage`):

```
benchmark.rpc_call      client.prepare_request    client.send_request
client.recv_header      client.recv_payload       server.read_header
server.read_payload     server.dispatch           server.response_queue
server.send_response    urma.write_total          urma.write_copy
urma.post_send          urma.wait_send_completion urma.read_wait_completion
urma.read_copy          urma.read_view            raw.client_write
```

线程局部采样,输出每阶段 `count / avg / p99 / p9999 / max`(μs)。通过 `configure(enabled, sample_rate)` / `record()` / `record_since(begin)` / `print(ostream)` 控制。

**延迟输出**含 avg/min/p50/p90/p99/p999/max(μs);**吞吐输出**含 request rate 与 payload MiB/s。

### 4.7 已知约束 / 注意点(开发须知)

1. **Send/recv credit 模型**:URMA 吞吐基准**不支持**单连接多协程并发(会 overrun 当前 credit 模型触发 `WR_FLUSH_ERR`),因此 `--concurrency` 仅作兼容选项保留,实际"每连接一 worker"。
2. **内存自动上调**:`--max-memory-mib`(默认 256MiB)按 `--connections` × `--queue-depth` × `--payload` 计算,不足时进程内自动抬高。
3. **profile 开销**:长吞吐测试建议用 `--profile-sample-rate` 降低采样以减少内存与计时开销。
4. **CTP 4KB 限制**:`bonding_dev_0` CTP 单包最大 4KB,默认 `--buffer-size` 即 4096。
5. **诊断建议**(来自 README):
   - `sink` 远高于 `echo` → 瓶颈在响应序列化/发送;
   - `sink` 也低 → 聚焦请求读取、RPC 分发、URMA 接收/轮询;
   - `--transport raw` 测纯 ingress。
6. **GDR(GPUDirect RDMA)** 走 IBV 路径(`YLT_ENABLE_IBV AND YLT_ENABLE_CUDA`),见 `examples/rdma_example/gdr_example.cpp`。

---

## 5. 构建系统

### 5.1 CMake(主)

入口 `CMakeLists.txt`,逻辑拆分到 `cmake/*.cmake`:

| 文件 | 职责 |
|---|---|
| `build.cmake` | 检测 C++ 标准(C++20 优先,回落 C++17);gcc `-fcoroutines`、MSVC `/bigobj`、libc++ 开关、字节序、ccache、clang 用 lld |
| `config.cmake` | 特性开关(见下表) |
| `develop.cmake` | 开发选项(`BUILD_EXAMPLES` 等,见下表) |
| `subdir.cmake` | glob `src/*`,为每个模块生成 `BUILD_<MODULE>`,按条件 add_subdirectory |
| `install.cmake` | 定义 INTERFACE target `yalantinglibs::yalantinglibs`,生成 `yalantinglibsConfig.cmake` 系列 |
| `utils.cmake` | 通用辅助 |
| `Find/` | `find_openssl.cmake`、`find_ntls.cmake`、`find_ibverbs.cmake`、`Finduring.cmake` |

**特性开关**(`cmake/config.cmake`):

| 选项 | 默认 | 链接 |
|---|---|---|
| `YLT_ENABLE_DL`(L32) | ON | dl(栈回溯) |
| `YLT_ENABLE_SSL`(L43) | OFF | OpenSSL |
| `YLT_ENABLE_NTLS`(L55) | OFF | 铜锁(Tongsuo) |
| `YLT_ENABLE_IBV`(L70) | OFF | libibverbs(RDMA verbs) |
| `YLT_ENABLE_URMA`(L81) | OFF | liburma(URMA) |
| `YLT_ENABLE_PMR`(L104) | OFF | pmr 分配器优化 |
| `YLT_ENABLE_IO_URING`(L114) | OFF | liburing(网络) |
| `YLT_ENABLE_FILE_IO_URING`(L128) | OFF | liburing(文件) |
| `YLT_ENABLE_STRUCT_PACK_UNPORTABLE_TYPE`(L143) | OFF | struct_pack 宽字符等 |
| `YLT_ENABLE_STRUCT_PACK_OPTIMIZE`(L149) | OFF | struct_pack 优化(增加编译时间) |

> `YLT_ENABLE_CUDA`(根 `CMakeLists.txt`,`CMAKE_CUDA_ARCHITECTURES 86`)**未用 `option()` 声明**,仅在 `config.cmake:92` 被 `if(YLT_ENABLE_CUDA)` 消费,需调用方显式 `-DYLT_ENABLE_CUDA=ON`。

**开发选项**(`cmake/develop.cmake`):

| 选项 | 默认 |
|---|---|
| `BUILD_EXAMPLES`(L3) | ON |
| `BUILD_BENCHMARK`(L7) | ON |
| `BUILD_UNIT_TESTS`(L11) | ON |
| `COVERAGE_TEST`(L18) | OFF |
| `GENERATE_BENCHMARK_DATA`(L29) | ON |
| `CORO_RPC_USE_OTHER_RPC`(L34) | OFF |
| `ENABLE_SANITIZER`(L38) | ON(Debug + gcc/clang,ASan) |
| `ENABLE_TSAN`(L40) | OFF |
| `ENABLE_WARNING`(L69) | OFF |

**模块开关机制**(`cmake/subdir.cmake`):对 `src/*` 每个子目录大写化为 `BUILD_<MODULE>` —— `ENABLE_CPP_20` 时默认 ON,否则 OFF;但 **C++17 下强制 ON** 四个序列化模块:`BUILD_STRUCT_PACK/JSON/XML/YAML`。

**典型命令:**

```bash
# 普通构建(Linux)
cmake -S . -B build
cmake --build build -j
cd build && ctest -j 1 -V

# URMA 构建(当前主线)
cmake -S . -B build -DYLT_ENABLE_URMA=ON -DBUILD_EXAMPLES=ON
cmake --build build -j

# 仅构建 URMA 基准目标
cmake --build build --target coro_rpc_urma_benchmark -j
# 产物:build/output/examples/coro_rpc/coro_rpc_urma_benchmark
```

### 5.2 Bazel(辅)

- `.bazelrc`(按平台 C++20 配置,Bzlmod 启用)、`MODULE.bazel`、`WORKSPACE(.bzlmod)`、`bazel/defs.bzl`、根 `BUILD.bazel`。
- Bazel 覆盖**远不如 CMake 完整**:根 `BUILD.bazel` 定义 `ylt` cc_library,但仅 `easylog_test` 等少量目标作为 `cc_test` 接入。测试矩阵以 CMake 为准。

### 5.3 安装与分发

- CMake `find_package(yalantinglibs CONFIG REQUIRED)` + `target_link_libraries(... yalantinglibs::yalantinglibs)`。
- 分发渠道:vcpkg(`vcpkg.json`,声明 `openssl`)、Homebrew(`brew install yalantinglibs`)、FetchContent、add_subdirectory。
- 安装控制选项:`INSTALL_THIRDPARTY` / `INSTALL_STANDALONE` / `INSTALL_INDEPENDENT_*`(决定 vendored 依赖是否独立安装)。
- 输出二进制落 `./build/output/`(`CMAKE_RUNTIME_OUTPUT_DIRECTORY`)。

> ⚠️ `vcpkg.json` 中 `version-string` 仍为 `0.1.0`,而 CMake 与 `version.hpp` 均为 `0.6.0`,manifest 版本相对陈旧。

---

## 6. 测试与基准

### 6.1 单元测试

- **框架**:**doctest**,vendored 单头 `src/include/doctest.h`(CC0)。每个模块 `tests/main.cpp` 用 `DOCTEST_CONFIG_IMPLEMENT` + `doctest::Context(...).run()`。
- **位置**:按模块 `src/<模块>/tests/`,无顶层 `tests/`。
- **规模**:约 **75** 个测试源,12 个模块。struct_pack(21)、coro_rpc(17)、coro_io(8 + ibverbs/ 4)、coro_http(7)覆盖最全。
- **运行**:CTest 集成(`add_test(NAME <x>_test COMMAND <x>_test)`);产物落 `build/output/tests/`。
- **特殊**:
  - `coro_rpc/tests/openssl_files/` 通过 POST_BUILD 拷贝到测试旁;
  - coro_rpc 测试以 `UNIT_TEST_INJECT` 编译(经 `inject_action.hpp` 注入故障),并启用 `STRUCT_PACK_ENABLE_UNPORTABLE_TYPE`;
  - `coro_rpc/tests/test_gdr.cpp` 仅当 `YLT_ENABLE_IBV AND YLT_ENABLE_CUDA` 编译;
  - `coro_io/tests/ibverbs/` 为 IBV 嵌套子套件(test_gdr / test_ib_socket / test_device / ib_socket_pressure_test)。

### 6.2 基准测试

按模块 `src/<模块>/benchmark/`,受 `BUILD_BENCHMARK` + `GENERATE_BENCHMARK_DATA` 控制:

- **struct_pack**:跨库序列化对比(vs protobuf / flatbuffers / msgpack,`find_package(... QUIET)` 自动探测);
- **coro_rpc**:`coro_rpc_benchmark_server/client` + `data_gen`,`api/` 含 Monster.h / Rect.h / rpc_functions.hpp / ValidateRequest.h;
- **coro_http / metric / easylog / util(ff_ptr)** 各自 `main.cpp`。
- **URMA 专项基准** `coro_rpc_urma_benchmark`(见 §4.6)。

### 6.3 故障注入

coro_rpc 测试启用 `UNIT_TEST_INJECT`,配合 `inject_action.hpp` 在 RPC 各阶段注入错误,验证错误路径与 ASan/TSan 下的一致性。

---

## 7. 文档与示例

### 7.1 文档站

- `website/`(VitePress + Doxygen):`website/docs/zh/`(中文,最全)、`website/docs/en/`(英文,部分)。
- 中文分类:`guide/`、`coro_rpc/`、`coro_http/`、`struct_pack/`、`struct_pb/`、`struct_xxx/`、`reflection/`、`metric/`、`easylog/`、`yalantinglibs-dev-guidelines.md`。
- 英文相对缺失 easylog / metric / reflection / struct_xxx。
- URMA 专项:`website/docs/public/urma_rpc_usage.html`(独立 HTML,尚未并入站点导航)。

### 7.2 示例

按模块 `src/<模块>/examples/`。重点:

- **coro_rpc**:`base_examples/`(client/server、并发、client_pool、load_balancer、ntls_*)、`file_transfer/`、`rdma_example/`(含 `gdr_example.cpp`)、`urma_example/`、`urma_benchmark/`、`user_defined_rpc_protocol/rest_rpc/`(`CORO_RPC_USE_OTHER_RPC`)。
- **跨语言**:`base_examples/py_example/`(pybind11 + Python 测试脚本)、`base_examples/go_example/`(Go interop)。
- **coro_http**:`example.cpp`、`load_balancer.cpp`、`chat_room.cpp`(WebSocket)、`client.html`、`ntls_*`、`py_example/`。
- **struct_pack**:`basic_usage.cpp`、`non_aggregated_type.cpp`、`serialize_config.cpp`、`user_defined_serialization.cpp`、`derived_class.cpp`。

### 7.3 URMA 参考文档

- `.claude/skills/query-urma-docs/`:三份中文文档(API Guide / User Guide / QuickStart),可通过该 skill grep 检索。
- `.claude/plans/urma-ctp-implementation-plan.md`、`.claude/plans/urma-ctp-next-steps.md`:URMA CTP 实现计划。

---

## 8. CI 与工具链

### 8.1 CI 流水线(`.github/workflows/`,14 个)

所有构建作业统一执行 `cmake build` + `ctest -j 1 -V`:

| 流水线 | 覆盖 |
|---|---|
| `ubuntu_gcc.yml` | Ubuntu 22.04 GCC,Debug × {SSL ON/OFF},含 `ubuntu_gcc_tsan`、`ubuntu_gcc_for_liburing`、`ubuntu_gcc9` 子作业 |
| `ubuntu_clang.yml` | Ubuntu 22.04 Clang |
| `windows.yml` | Win Server 2022 MSVC,Release × {amd64, x86} |
| `mac.yml` | macOS Apple Clang,Debug × {SSL ON/OFF} |
| `s390x.yml` | IBM s390x(大端),仅 struct_pack 验证字节序 |
| `ntls_test.yml` | 铜锁 NTLS 端到端 |
| `bazel_clang.yml` | Bazel build + test(Clang 17) |
| `clang-format.yml` | `git-clang-format --diff HEAD^`,有 diff 即失败 |
| `code-coverage.yml` / `linux_llvm_cov.yml` | 覆盖率(Codecov / llvm-cov) |
| `website.yml` | 文档站构建发布 |
| `update_cache.yml` / `clean_cache.yml` | ccache 维护 |
| `comment.yml` | PR 评论辅助 |

### 8.2 工具链配置

- **格式化**:`.clang-format`(Google base,自定义大括号包裹)。**无 `.clang-tidy`** —— CI 仅做格式检查。
- **脚本**:`coverage_gen.sh`(根,覆盖率)、`scripts/run_ntls_e2e_tests.sh`(NTLS e2e)、`website/generate.sh`(文档生成)。
- 无 `CMakePresets.json`、无 toolchain 文件、无 cmake-format 配置。

---

## 9. 依赖与生态

### 9.1 vendored(随库安装)

`include/ylt/thirdparty/`:
- **async_simple** — 阿里协程 / uthread 运行时(含 x86_64/aarch64/ppc64le Linux、arm64/x86_64 Darwin 汇编)。协程基础。
- **asio** — Boost.Asio standalone(完整 vendored)。I/O 后端。
- **frozen** — serge-sans-paille/frozen,编译期容器/map。

`include/ylt/standalone/`:
- **cinatra** — coro_http 所基于的 HTTP 头。
- **iguana** — struct_json/xml/yaml 所基于的反射序列化。

### 9.2 系统依赖(按开关)

| 依赖 | 开关 | CMake Find |
|---|---|---|
| OpenSSL | `YLT_ENABLE_SSL` | `Find/find_openssl.cmake` |
| 铜锁(Tongsuo) | `YLT_ENABLE_NTLS` | `Find/find_ntls.cmake` |
| libibverbs | `YLT_ENABLE_IBV` | `Find/find_ibverbs.cmake` |
| liburma | `YLT_ENABLE_URMA` | `-lurma` |
| liburing | `YLT_ENABLE_IO_URING` / `_FILE_IO_URING` | `Find/Finduring.cmake` |
| CUDA | `YLT_ENABLE_CUDA` | 标准 CMake CUDA |
| protobuf / flatbuffers / msgpack | (基准自动探测) | `find_package(... QUIET)` |

### 9.3 许可证归属(`NOTICE`)

- 自有代码:Apache-2.0,Copyright 2019–2022 Alibaba。
- `struct_pack/texpr.hpp` Apache-2.0;`util/expected.hpp` BSD-3;`src/include/doctest.h` CC0;`struct_pack/pp` Boost-1.0。
- 设计致谢:zpp_bits、reflect-cpp、msgpack、rest_rpc。

---

## 10. 编译器与平台支持

| 平台 | C++17 | C++20 |
|---|---|---|
| Ubuntu 22.04 | GCC 9+ / Clang 6+ | GCC 10+ / Clang 13+ |
| macOS | Apple Clang 14+ | Apple Clang 14+ |
| Windows | MSVC 14.20+ | MSVC 14.29+(含 MinGW-w64 GCC,链接 `ws2_32/mswsock/dbghelp`) |
| IBM s390x | — | 大端验证(仅 struct_pack) |

C++17 仅支持序列化子集(struct_pack/json/xml/yaml、easylog);C++20 需求覆盖 coro_rpc / coro_io / coro_http 全功能。

---

## 11. ⭐ 开发扩展点与约定

为后续开发需求梳理的扩展入口与既有约定:

### 11.1 新增库模块

1. 头文件置于 `include/ylt/<新模块>/`,提供 facade 头(对外聚合,真实实现可放 `impl/`)。
2. `src/<新模块>/{examples,tests,benchmark}/`,每个子目录自带 `CMakeLists.txt`(判断顶层项目或用 `find_package`)。
3. `cmake/subdir.cmake` 会自动 glob `src/*` 生成 `BUILD_<新模块>` 开关 —— 无需手动注册。
4. 测试用 doctest(`src/include/doctest.h`),`tests/main.cpp` 实现 `DOCTEST_CONFIG_IMPLEMENT`。

### 11.2 新增 RPC 协议

- 协议实现置于 `coro_rpc/impl/protocol/`(参考 `coro_rpc_protocol.hpp`)。
- 启用开关 `CORO_RPC_USE_OTHER_RPC`,示例见 `examples/user_defined_rpc_protocol/rest_rpc/`。
- 通过 `socket_wrapper_t` 的类型擦除接入不同底层传输。

### 11.3 新增传输后端(URMA / IBV 等)

参考 URMA 的分层(§4.2):
1. **C 绑定层**(`urma/` 风格)—— 直接对接底层库的头。
2. **C++ 封装层**(`coro_io/urma/` 风格)—— 提供 socket / buffer_pool / io 协程封装,接入 `coro_io::socket_wrapper_t`。
3. **coro_rpc 集成**(`coro_connection.hpp` / `coro_rpc_protocol.hpp` / `coro_rpc_client.hpp` 的条件路径)。
4. **基准与剖析**—— 在 `urma_benchmark_profile.hpp` 的 `enum class stage` 增补阶段,并在 `urma_benchmark` 示例暴露 CLI。

> 当前 IBV 与 URMA 并存,IBV 支持 GDR(GPU direct),URMA 为新主线但**尚不支持**单连接多协程并发(credit 模型约束,见 §4.7)。

### 11.4 命名与代码约定

- 命名空间:`ylt::`(库顶层)、`coro_rpc::`、`coro_io::`、`coro_http::` 等。
- 协程返回类型统一 `async_simple::coro::Lazy<T>`;异步启动用 `async_simple::Future<T>`。
- facade 头(`coro_rpc_server.hpp` 等保持极薄)与 `impl/` 实现分离的约定,沿用即可。
- 格式化遵循 `.clang-format`(CI 强制),无 clang-tidy。

### 11.5 当前分支已知事项 / 待办

- URMA URMA/CTP 仍处活跃开发,文档尚未并入 `website/docs/zh|en` 导航(仅 `public/urma_rpc_usage.html`)。
- `--concurrency` 在 URMA 下为兼容占位(受 credit 模型限制)。
- `vcpkg.json` 版本号 `0.1.0` 与实际 `0.6.0` 不一致。
- 无 `.clang-tidy`,静态检查仅格式。

---

*文档基线:`supercache_dev` 分支,HEAD `87d961d`(add performance metric)。如需补充某模块 API 细节或扩展具体开发场景,可在此基础上进一步展开。*
