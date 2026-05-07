# 已知限制

本文集中列出 `Esp32Base` 第一版明确接受的边界。它们不是待补功能，而是为保持库轻量、可裁剪、可维护而刻意限定的范围。

## 1. 安全边界

- Web 使用 HTTP Basic Auth，不提供 HTTPS。
- Basic Auth 明文传输，只用于降低误操作风险，不抵御同一局域网内的嗅探、MITM 或主动攻击。
- Web Auth 密码不在 HTML、JSON 或 API 响应中明文输出；持久化时只保存 salted SHA-256 摘要；`INFO` 日志会明文输出 Web 用户名和密码。
- OTA 只提供上传认证和 SHA256 完整性校验，不提供固件加密、签名信任链或差分升级。
- OTA 与 Web Auth 配置解耦；关闭 Web Auth 时 OTA 仍可访问，但没有密码保护，风险由应用和用户自行承担。
- Web 配置页面在用户通过 Basic Auth 后会回显当前 WiFi 密码，这是运维查看当前配置所需。该回显与日志的 INFO 级密码屏蔽策略分别考虑；生产固件应避免在公共网络环境下打开配置页。

## 2. Web 边界

- Web 基于 Arduino `WebServer` 同步模型。
- 长时间 handler 会阻塞其他请求，建议单次 handler < 200ms。
- 不支持 WebSocket、SPA、大型前端资源管理器、多用户权限和会话系统。
- OTA 上传页只复用 Web Basic Auth，不提供第二套认证。

## 3. 网络边界

- 有已保存 WiFi 凭证但连接失败时，库不会自动进入 AP/config portal，而是持续 STA 重连。
- 进入 AP/config portal 只发生在无凭证、显式 `startConfigPortal()` 或应用自定义策略下。
- 该策略用于防止量产设备在路由器临时故障时被陌生人通过 AP 修改凭证。
- 如果应用面向消费场景，需要换路由器后自动配网，应由应用在长 backoff 后显式调用 `Esp32BaseWiFi::startConfigPortal()`，并配合按键长按、状态灯或屏幕提示等用户确认方式。
- deep sleep 期间 STA、AP、DNS、Web 均不可访问；唤醒后按新启动流程恢复。
- Captive Portal DNS 对所有查询返回 AP IP，不提供按域名扩展的 `addCaptiveTarget()`。

## 4. 并发边界

- `Esp32BaseBus::publish()` 只允许在 Arduino loop 任务调用。
- WiFi callback、timer callback、ISR、LWIP 回调不得直接 publish，只能入队后在 `handle()` 中处理。
- 本库不提供跨任务事件总线，不包装 FreeRTOS 队列作为公开通用消息系统。

## 5. 存储边界

- Config 后端为 ESP32 NVS，只适合小配置，不适合大量数据或高频日志。
- Config 字符串 API 支持最大 3999 字节可见内容，并使用固定 scratch buffer 读取和比较，避免 Arduino `String` 造成 heap 碎片；它仍然不适合大量数据或高频日志。
- NVS 写满时 set API 返回 false，库不自动删除业务数据。
- `clearLibraryNamespaces()` 只清理 `eb_` 前缀的库 namespace。
- 应用不得使用 `eb_` 前缀作为自己的 NVS namespace。
- LittleFS 默认不自动格式化；首次空分区 mount failed 不会 halt。

## 6. 文件系统边界

- LittleFS 用于小型配置文件、诊断文件和 Web 静态小资源。
- 业务需要二进制定长日志时，应通过 `Esp32BaseFs::readBytesAt()` / `writeBytesAt()` 做分页读取和固定位置覆盖，不直接依赖 LittleFS 或 Arduino `File`。
- `writeBytesAt()` 只覆盖已有文件内容，不创建、不扩展文件；环形文件容量需要业务初始化。
- 不提供大型文件管理器。
- 不保证在 OTA 写 flash 时并行执行大量 FS 写入的实时性。
- Fs 没有 maintenance handle；挂载后按显式 API 操作。

## 7. 资源边界

- 第一版不依赖 PSRAM。
- 即使板子有 PSRAM，默认资源预算也按无 PSRAM 设计。
- ESP32-C3 单核、内存和 wake source 更受限，默认容量更保守。
- FULL profile 必须通过实机资源表确认，不能只看依赖声明。

## 8. 兼容边界

- 支持 Arduino ESP32 Core 2.0.14+ 和 3.0.4+。
- Watchdog、WiFi event、mDNS、brownout 控制必须使用版本条件编译隔离。
- mDNS `stop()` 对外语义是停止广告，不承诺释放底层 mDNS 全部资源。
- ESP32-C3 不支持的 wake source 返回 false 并输出 warn。

## 9. OTA 边界

- OTA 只支持整包升级。
- 未提供 SHA256 时允许跳过完整性校验；提供时必须严格校验。
- SHA256 校验失败不得调用 `Update.end(true)`，不得重启到新固件。
- 默认不关闭 brownout detector；临时关闭 brownout 仅作为显式开启的风险选项。

## 10. 日志边界

- Core Log 只输出 Serial 和可选 sink callback，不依赖 FS。
- 文件日志由 Runtime/FS 的 `Esp32BaseFileLog` 提供；CORE 和默认 NET 不具备文件日志能力。
- `INFO` 日志明文输出 WiFi 名称/密码和 Web Auth 用户名/密码，用于业务接入和现场调试。
- 日志访问权限由应用和部署环境控制。
- 用户 sink callback 同步执行，不得长时间阻塞。

## 11. 发布后演进边界

发布后只接受：

- bug fix。
- Arduino Core 兼容修复。
- 芯片适配修复。
- 文档修正。
- 测试补充。

发布后不接受：

- 新增大型模块。
- 大规模架构调整。
- 破坏公开 API 的非必要改动。
- 改变 profile 语义。
