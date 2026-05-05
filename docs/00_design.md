# 设计说明

## 1. 项目目标

`Esp32Base` 是 ESP32 系列应用项目的基础运行底座，目标是一次构建出成熟、稳定、资源可控的基础库。

本轮重构不考虑历史兼容性。旧代码、旧示例、旧文档已归档到 `design-history/`，仅作为历史参考，不作为实现约束。

最终目标：

- 为 ESP32 / ESP32-S3 / ESP32-C3 提供统一基础能力。
- 让只使用部分能力的应用真正节省 Flash、RAM 和启动资源。
- 用少量 profile 覆盖真实设备常见组合。
- 所有重模块可编译期裁剪。
- 以可靠 OTA、配置落盘、配网体验和长时间运行稳定性为核心质量标准。

## 2. 设计原则

### 2.1 Core 最小化

Core 只包含：

- 日志
- NVS 小配置
- 系统诊断和生命周期

Core 不包含：

- Event Bus
- WiFi
- Web
- OTA
- LittleFS
- Watchdog
- Sleep

原因：极简应用不应为无用静态表、WiFi 栈、WebServer、Update、LittleFS 付资源成本。

### 2.2 Profile 优先，底层宏可覆盖

普通用户通过 profile 选择场景。

高级用户可用 `ESP32BASE_ENABLE_*` 精细覆盖 profile 默认值。

规则：

- profile 先展开默认值。
- 用户显式 `ESP32BASE_ENABLE_*` 优先。
- 展开结束后执行依赖检查。
- 无效组合编译时报错。

### 2.3 状态机和延迟启动

`begin()` 不阻塞等待：

- WiFi 连接
- NTP 同步
- mDNS 启动
- Web 启动
- OTA ready

这些能力由 `handle()` 按依赖条件推进。

### 2.4 量产可靠性优先

必须闭环：

- restart / sleep 前 `flushAll()`
- OTA SHA256
- OTA rollback
- OTA Watchdog remove / restore
- OTA 期间暂停 NVS deferred flush
- Captive Portal DNS
- LittleFS 挂载失败不 halt
- NVS namespace/key 长度校验
- Arduino Core 2.x / 3.x 兼容
- ESP32-C3 差异

### 2.5 发布后架构冻结

本库目标是一次定型。文档评审和实施完成后，只接受：

- bug fix
- Arduino Core 兼容性维护
- 芯片适配修正
- 文档修正
- 测试补充

不再做大规模架构重构。

## 3. 明确不做

- ESP8266 支持
- 跨芯片 HAL
- MQTT 封装
- HTTP Client 封装
- WebSocket
- HTTPS
- 多用户权限
- 复杂 Scheduler
- 云平台绑定
- 大型 Web 管理平台
- 大型前端 SPA
- 模板 policy 架构
- 静态模块注册表

文件日志作为 Runtime/FS 可选模块提供，不属于 Core。启用 FS 的 profile 默认开启文件日志；CORE 不为 LittleFS 或文件日志付成本。

## 4. 质量目标

### 4.1 资源目标

必须证明：

- CORE 不链接 WiFi / WebServer / Update / LittleFS。
- NET 不链接 WebServer / Update。
- WEB 不链接 Update。
- 关闭 Bus / Fs / Health 不产生静态对象。

证明方式：

- PlatformIO 编译。
- map 文件检查。
- `text / data / bss` 记录。
- 实机 free heap / min heap 记录。

### 4.2 可靠性目标

发布前必须验证：

- OTA 中途断电不变砖。
- OTA 后未 mark valid 可回滚。
- OTA SHA256 错误不切换分区。
- NVS 写满不崩溃。
- LittleFS 挂载失败不影响其他模块。
- Captive Portal 在主流系统可用。
- WiFi 断线后可恢复。
- ESP32-C3 单核重负载不误触 watchdog。

## 5. 发布冻结条件

只有满足以下条件才冻结第一版发布：

- docs 与代码全部评审通过。
- Profile 集合冻结。
- 公开 API 契约冻结。
- 编译矩阵与核心裁剪验证通过。
- 发布检查清单冻结。
