# URMA CTP 实现计划

## 当前状态

### 已完成
1. `circle_buffer` 提取到 `coro_io/detail/circle_buffer.hpp`
2. `urma_device.hpp` 类型冲突修复（`urma_device_wrapper_t`）
3. `urma_socket.hpp` 基础重构
4. JFC/JFR/Jetty 创建流程修正

### 已知问题
1. `urma_query_jetty` 签名不正确
2. `urma_import_jetty` 参数结构错误
3. ASIO 协程兼容性问题
4. Socket wrapper 接口不匹配

---

## URMA CTP API 正确用法

### 1. JFC 创建 (Completion Channel)
```c
urma_jfc_cfg_t jfc_cfg = {};
jfc_cfg.depth = 64;
jfc_cfg.flag.value = 0;
jfc_cfg.jfce = nullptr;  // polling mode
jfc_cfg.user_ctx = 0;
urma_jfc_t* jfc = urma_create_jfc(ctx, &jfc_cfg);
```

### 2. JFR 创建 (Receive Queue)
```c
urma_jfr_cfg_t jfr_cfg = {};
jfr_cfg.depth = recv_cnt;
jfr_cfg.flag.value = 0;
jfr_cfg.trans_mode = URMA_TM_RM;
jfr_cfg.max_sge = 1;
jfr_cfg.min_rnr_timer = 12;
jfr_cfg.jfc = jfc;
jfr_cfg.token_value = {};
urma_jfr_t* jfr = urma_create_jfr(ctx, &jfr_cfg);
```

### 3. Jetty 创建 (CTP Mode)
```c
urma_jetty_cfg_t jetty_cfg = {};
jetty_cfg.flag.bs.share_jfr = 1;
jetty_cfg.jfs_cfg.depth = send_cnt + 1;
jetty_cfg.jfs_cfg.flag.value = 0;
jetty_cfg.jfs_cfg.trans_mode = URMA_TM_RM;
jetty_cfg.jfs_cfg.jfc = jfc;
jetty_cfg.jfs_cfg.user_ctx = 0;
jetty_cfg.shared.jfr = jfr;
urma_jetty_t* jetty = urma_create_jetty(ctx, &jetty_cfg);
```

### 4. Jetty ID 获取
```c
// 通过 jetty->jfs_id.id 获取
uint32_t jetty_id = jetty->jfs_id.id;
```

### 5. Segment 注册
```c
urma_seg_cfg_t seg_cfg = {};
seg_cfg.va = (uint64_t)buffer;
seg_cfg.len = buffer_size;
seg_cfg.flag.bs.access = URMA_ACCESS_READ | URMA_ACCESS_WRITE;
seg_cfg.flag.bs.token_policy = URMA_TOKEN_NONE;
urma_target_seg_t* tseg = urma_register_seg(ctx, &seg_cfg);
```

### 6. 导入远端 Jetty
```c
urma_rjetty_t remote = {};
remote.jetty_id.eid = peer_eid;
remote.jetty_id.id = peer_jetty_id;
remote.trans_mode = URMA_TM_RM;
remote.type = URMA_JETTY;
remote.tp_type = URMA_RTP;  // or URMA_CTP
urma_target_jetty_t* remote_tjetty = urma_import_jetty(ctx, &remote, nullptr);
```

### 7. 发送数据 (SEND)
```c
urma_sge_t sge = {
    .addr = (uint64_t)buffer,
    .len = data_len,
    .tseg = local_tseg
};
urma_sg_t src = {
    .sge = &sge,
    .num_sge = 1
};
urma_send_wr_t send_wr = {
    .src = src
};
urma_jfs_wr_t jfs_wr = {
    .opcode = URMA_OPC_SEND,
    .send = send_wr,
    .tjetty = remote_tjetty
};
urma_post_jetty_send_wr(jetty, &jfs_wr, &bad_wr);
```

### 8. 轮询完成
```c
urma_cr_t cr[8];
int cnt = urma_poll_jfc(jfc, 8, cr);
for (int i = 0; i < cnt; ++i) {
    // cr[i].completion_len - 传输字节数
    // cr[i].status - 状态
    // cr[i].flag.bs.s_r - 0=send, 1=recv
}
```

---

## 待修复清单

### 高优先级
- [ ] `urma_socket.hpp`: 修复 `urma_query_jetty` 调用
- [ ] `urma_socket.hpp`: 修复 `urma_import_jetty` 参数
- [ ] `urma_socket.hpp`: 正确获取 Jetty ID
- [ ] `socket_wrapper.hpp`: 添加缺失接口

### 中优先级
- [ ] ASIO 协程兼容性问题
- [ ] `await_ready` Future 使用错误
- [ ] `async_read/write` 调用方式

### 低优先级
- [ ] `urma_socket_info` 序列化格式
- [ ] 连接握手协议

---

## 参考文档

- URMA API Guide: `.claude/skills/query-urma-docs/URMA API Guide.ch.md`
- URMA User Guide: `.claude/skills/query-urma-docs/URMA User Guide.ch.md`
- URMA 头文件: `include/ylt/urma/urma_api.h`, `urma_types.h`