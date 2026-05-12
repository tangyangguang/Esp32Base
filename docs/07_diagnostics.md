# 诊断与测试

## 1. 验证策略

验证分为发布前自动化编译检查、符号裁剪检查和实机验收。

自动化编译检查必须先覆盖 profile、芯片和 Arduino Core 兼容性；实机验收用于确认 WiFi、Web、OTA、Sleep、Watchdog、Captive Portal 等运行时行为。

任何验证发现裁剪、兼容、OTA 或 Captive Portal 缺陷，必须先修复代码和文档，再重新执行相关矩阵。

## 2. 发布前编译与裁剪验证

### 2.1 Profile 裁剪

准备 7 个空 main：

- CORE
- RUNTIME
- NET
- NET_RUNTIME
- WEB
- WEB_RUNTIME
- FULL

检查：

- map 文件。
- firmware size。
- `text / data / bss`。
- 重模块符号。

可用 `scripts/check_trim_symbols.py` 在 `examples/basic` 构建后快速检查最终 ELF 是否包含被禁用 profile 的重模块符号。

验收：

- CORE 不链接 WiFi/WebServer/Update/LittleFS。
- NET 不链接 WebServer/Update/LittleFS，除非显式启用 FS。
- WEB 不链接 Update。
- FS 关闭时 FileLog 不拉入 LittleFS。

### 2.2 Arduino Core 2.x / 3.x

验证：

- 2.0.14。
- 3.0.4。
- ESP32。
- ESP32-S3。
- ESP32-C3。

重点：

- Watchdog。
- WiFi event。
- Sleep。
- mDNS。
- ESP32-S3 USB-CDC 串口日志。
- brownout 控制编译兼容性。

### 2.3 OTA + Watchdog + NVS pause + SHA256

验证：

- 大固件上传。
- Watchdog remove/restore。
- OTA 期间 1Hz NVS 写入。
- SHA256 正确。
- SHA256 错误。

### 2.4 WiFi event 入队

验证：

- WiFi callback 不 publish。
- loop 中 publish。
- 事件延迟。
- 事件丢失。
- wrong task publish 检查。
- `ESP32BASE_BUS_STRICT_TASK_CHECK=1` 下 wrong task assert。

### 2.5 Captive Portal

验证设备：

- iPhone。
- Android。
- macOS。
- Windows。

验证：

- 连接 AP 后弹出或跳转配网页。
- DNS 拦截。
- 所有 DNS 查询通配到 AP IP。
- 非内置路径重定向。
- WiFi 表单拒绝空 SSID，允许空密码用于开放 WiFi。
- 重启按钮二次确认。
- OTA 上传页进度显示。

### 2.6 Config / Fs 语义

验证：

- pending capacity 默认返回 8。
- 8 个不同 key 可入队，第 9 个返回 false。
- `setXxxDeferred()` 后立即 `getXxx()` 返回 pending 新值。
- 同 key 多次 deferred 写入合并。
- POD blob 写入/读取成功，最大长度边界为 256 字节。
- POD blob 重复写入相同值跳过 NVS 写入。
- POD blob deferred 后立即读取返回 pending 新值，`flushAll()` 后重启仍保持。
- OTA 上传期间 pending 可读，普通 deferred flush 暂停。
- `clearLibraryNamespaces()` 不清理业务 namespace。
- Fs 文本读写、二进制读写、追加、目录列举、rename、mkdir、rmdir。

### 2.7 FileLog

验证：

- FS profile 默认启用 WARN 文件日志。
- 示例中 INFO 文件日志生效。
- 默认 `4 × 32KB` 轮转正确。
- INFO/DEBUG 仅在 file level 降低后写文件。
- INFO/DEBUG 使用 `1KB / 2s` 缓存。
- WARN/ERROR 立即写入。
- clear 幂等。
- restart、deep sleep、OTA success、rollback restart 前 flush。

## 3. CI 矩阵

CI 应覆盖核心矩阵，release 前执行完整矩阵。

推荐：

- PR 跑核心矩阵。
- release 跑完整矩阵。

核心矩阵：

- CORE。
- NET_RUNTIME。
- FULL。
- ESP32 / ESP32-S3 / ESP32-C3。
- 主流 Arduino Core 3.x。

完整矩阵：

- 7 个 profile。
- 3 个芯片。
- Arduino Core 2.x / 3.x。

## 4. 实机验收

必须通过：

- CORE：无 AP、无 Web、无 FS 挂载。
- RUNTIME：Watchdog、Sleep、LittleFS 文件读写。
- NET：AP 配网到 STA。
- NET_RUNTIME：WiFi + Watchdog + Fs 长时间运行。
- WEB：Auth、状态 API、配网 API、自定义路由。
- WEB_RUNTIME：Web + Fs + Watchdog，无 OTA。
- FULL：OTA 成功、未授权拒绝、Watchdog 安全。
- OTA 默认认证拒绝。
- OTA SHA256 错误拒绝。
- OTA 中途断电 30% / 70% / 99%。
- OTA 后未 markValid 可回滚。
- OTA 期间高频 NVS 写入。
- Captive Portal 多系统。
- WiFi 配置页空 SSID 拒绝提交，空密码允许提交。
- WiFi 配置页超长 SSID / 密码拒绝提交。
- WiFi 配置页回显 SSID / 密码。
- 重启按钮二次确认。
- OTA 上传进度显示。
- 路由器掉电 5 分钟后恢复。
- 已保存凭证但连接失败时不进入 AP，持续重连并 backoff。
- NVS 写满。
- LittleFS 首次挂载失败不 halt。
- LittleFS 二进制文件读写和目录列举。
- FileLog 默认 `4 × 32KB` 轮转。
- Logs 页面可读取 history/current。
- Logs 页面清空后可继续写 current。
- JSON escape。
- ESP32-C3 单核重负载。
- ESP32-S3 USB-CDC 日志可用。
- mDNS 多次重连恢复。
- deferred write + restart 全部落盘。
- deferred write 后立即读返回新值。
- brownout during OTA 不变砖。

48 小时 soak：

- FULL profile，ESP32 / ESP32-S3 / ESP32-C3 各一台。
- 每秒 1 次 NVS deferred 写。
- 每 10 分钟 1 次 Web 状态查询。
- 每 30 秒 1 次 `health.tick` bus 事件。
- Health tick 未超过 loop 阈值时，默认每 30 分钟最多输出 1 条 DEBUG 日志，不污染 INFO 文件日志，也避免 DEBUG 文件日志刷屏。
- Health tick 窗口内最大 loop 间隔超过 `ESP32BASE_HEALTH_LOOP_WARN_MS` 时输出 WARN。
- `ESP32BASE_HEALTH_LOOP_WARN_MS` 默认 3000ms；高实时性业务可自行降到 1000/2000ms，长操作较多的应用可升到 5000ms。
- `ESP32BASE_HEALTH_DEBUG_LOG_INTERVAL_MS` 默认 1800000ms；设为 0 可关闭普通 DEBUG tick 日志。
- 至少包含一次路由器断电 5 分钟后恢复。
- 监控 min heap、reset count、loop max period、WiFi 自动重连成功率、NVS pending 积压。
- 任意意外重启、卡死、heap 持续退化或重连失败均视为不通过。

## 5. 启动诊断

启动日志必须包含：

- boot session 计数。
- profile。
- enabled modules。
- firmware info。
- hostname。
- reset reason。
- wake reason。
- heap。
- flash。
- NVS pending。
- WiFi state，如启用。
- FS state，如启用。
- Web routes，如启用。
- OTA state，如启用。
- FileLog state，如启用 FS。

日志要求：

- FileLog 初始化后必须输出 boot session 诊断块，使用 `boot` tag，包含 `boot_count`、reset/wake reason 及中文说明、固件、版本、build、profile、hostname、heap、flash；字段必须拆成多条短日志，避免单行超过日志缓冲导致截断。
- `boot_count` 是 `uint32_t` 启动会话计数，上电、手动复位、`ESP.restart()`、Watchdog、OTA 重启、deep sleep 唤醒都计入；不能把它解释为单纯上电次数。
- `reset_reason` 表示本次复位/启动来源；`wake_reason` 仅表示 sleep 唤醒来源，普通上电或普通复位时为 `undefined` 是正常状态；`reset_desc` / `wake_desc` 输出中文简明解释。
- restart/deep sleep 生命周期 reason 仍由 `Esp32BaseSystem::appendRestartLog()` / `restartLogCount()` 记录，与 `boot_count` 分开看。
- 启动日志按 begin 顺序输出，便于定位失败模块。
- `DEBUG` 编译级别下，启动流程输出模块初始化顺序，以及 Web/NTP/mDNS/OTA 延迟启动或停止原因；这些日志用于开发诊断，默认 `INFO` 构建不输出。
- NTP 对时前日志时间戳使用启动后毫秒数，例如 `[42442]`；对时后切换为绝对日期时间。
- NTP 默认按 UTC+8 输出本地时间，应用可通过 `ESP32BASE_NTP_GMT_OFFSET_SEC` / `ESP32BASE_NTP_DAYLIGHT_OFFSET_SEC` 覆盖。
- NTP 同步判断默认要求 epoch >= `ESP32BASE_NTP_SYNC_MIN_EPOCH`，默认值为 `1700000000UL`。
- NTP 未同步状态不输出周期性 WARN/DEBUG；只有明确的单次同步失败事件才应输出 WARN。
- 字节数同时显示 raw bytes 与 KB/MB。
- NTP 对时成功后输出实际时间、当前 uptime、推算 boot wall time。
- WiFi 连接、断开、重连 backoff、进入/退出 config portal 都必须有清晰日志。
- 文件日志默认 WARN；现场调试示例可开启串口 DEBUG，并将文件日志设为 INFO 方便通过 Logs 页面观察。
- Serial 日志和 FileLog 必须可独立控制；量产设备可通过 `Esp32BaseLog::setSerialLevel(Esp32BaseLog::NONE)` 关闭串口，同时保持 FileLog WARN/ERROR 或 INFO 正常写入。
- `ESP32BASE_LOG_LEVEL` 是编译期上限；如果编译为 `ESP32BASE_LOG_NONE`，日志宏被移除，FileLog 也不会收到日志。
- 普通页面 GET 慢请求、认证成功、路由注册和配置审计 skipped/read/deferred 属于 DEBUG 诊断；配置实际写入、flush、启动认证加载、WiFi/OTA/NTP/mDNS 状态、格式化、重启和认证失败应保留 INFO/WARN/ERROR。
- Config audit 可在 `Esp32Base::begin()` 前开启；begin 前开启可覆盖基础库初始化读写，begin 后开启只覆盖后续业务运行期访问。read audit 噪声较大，且多数为 DEBUG。
- `INFO` 日志明文输出 WiFi 名称/密码和 Web Auth 用户名/密码，便于业务接入和现场调试。

## 6. 运行诊断

Health 模块记录：

- uptime。
- loop max period。
- health.tick 时间。

其他诊断字段通过对应模块查询：

- heap / min heap：`Esp32BaseSystem`。
- reset reason：`Esp32BaseSystem`。
- WiFi state：`Esp32BaseWiFi`。
- FS state：`Esp32BaseFs`。
- FileLog state：`Esp32BaseFileLog`。

默认周期：

```cpp
ESP32BASE_HEALTH_TICK_INTERVAL_MS=30000
ESP32BASE_HEALTH_DEBUG_LOG_INTERVAL_MS=1800000
```

## 7. 故障诊断

必须记录：

- begin optional module failed。
- NVS 写入失败。
- NVS 写满。
- WiFi 连接失败和 backoff。
- 已保存凭证下持续重连，不自动进入 AP。
- Captive DNS 启动失败。
- Web auth 失败。
- WiFi 配置表单校验失败。
- FileLog append/rotate/clear 失败。
- OTA start/write/verify/end 失败。
- OTA progress。
- OTA 上传生命周期日志：开始、10% 阶段进度、成功或失败；进度日志必须低频，避免按 chunk 刷屏。
- LittleFS mount failed。
- Watchdog reset detected。
- rollback reason。

## 8. 维护回归

发布后 bug fix 必须补对应回归测试。

每次维护发布至少重新验证：

- CORE / NET_RUNTIME / FULL 编译裁剪。
- 受影响模块实机用例。
- OTA SHA256 正确和错误路径。
- WiFi 已保存凭证重连策略。
- NVS deferred read-through。
- JSON escape。
- 文档与头文件 API 一致。
