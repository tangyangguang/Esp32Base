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
- Watchdog 长操作策略：长操作不再从 task WDT 注销当前任务，分块 FS/OTA/日志路径必须定期 feed/yield；不可细分的底层调用如果卡死，仍应由 WDT 兜底复位。
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

### 2.7 System Diagnostic Logs (FileLog)

验证：

- FS profile 默认启用 WARN 系统诊断日志。
- 默认系统诊断日志路径为 `/esp32base/logs/system.log`，轮转段继续使用同名前缀加 `.1`、`.2`、`.3`。
- 示例中 INFO 系统诊断日志生效。
- 默认 `4 × 32KB` 轮转正确。
- ERROR 仅在 FileLog 模式为 ERROR/WARN/INFO 后写文件；WARN 仅在 WARN/INFO 后写文件；INFO 仅在 INFO 后写文件；DEBUG/VERBOSE 不作为系统诊断日志模式。
- INFO 使用 `1KB / 2s` 缓存。
- WARN/ERROR 立即写入。
- clear 幂等。
- restart、deep sleep、OTA success、rollback restart 前 flush。

### 2.8 App Events

仅在 `ESP32BASE_ENABLE_APP_EVENTS=1` 时验证：

- 默认 App Events store 路径为 `/esp32base/app-events/events.bin`。
- `Esp32BaseAppEventLog::append()` 可写入 info/warn/error 事件，`source/type/reason/object/text/code/value1..value3` 读取一致。
- FS 未 ready 时 `begin()` / `append()` 返回 false，不崩溃。
- 默认容量 1024 条，写满后环形覆盖，`readLatest(offset, limit)` 按最新优先分页。
- 重启后 header 和记录可恢复，`nextId()` 继续递增。
- 已存在 `/esp32base/app-events/events.bin` 但文件尺寸不匹配或不可读时必须进入 fault，不得自动删除或重建；只有文件缺失时 `begin()` 才可创建空 store。
- 满容量环形覆盖时，模拟 record 已写入但 header 未提交的重启；恢复后不得进入 fault，未提交槽应不可见，后续 append 仍可继续。
- Watchdog 启用后，在 setup、Web handler 或业务回调里同步 append/clear 不应因为 LittleFS flush 较慢触发 loopTask WDT。
- Esp32BaseFs 大块读写必须通过库内分块 I/O 和长操作 service 覆盖；业务一次保存 12KB 以上二进制数据、Web FS 上传/下载和 App Events/FileLog 写入都不应因为单次 LittleFS 同步 I/O 触发 task WDT。
- Web FS 覆盖上传中断或写入失败时，旧目标文件不应在上传阶段被先清空；失败后最多遗留可删除的同目录临时文件。断电遗留的临时文件不应阻塞下一次上传。
- 双 header 损坏、文件尺寸错误、写后校验失败等结构性问题进入 fault，不自动清空；单条记录 `crc16` 错误应跳过、暴露 `record_skipped`，完整扫描后 `count()` 应收敛到可读取记录数，并继续允许 append。
- clear 幂等，清空后可继续写入；业务恢复出厂或清空业务记录时可显式调用。
- System 页格式化 LittleFS 成功并重新 mount 后，App Events store 必须重新创建并清理旧运行态；FS 管理页上传、覆盖或删除 `/esp32base/app-events/events.bin` 后必须重新加载 App Events 运行态。
- Web 页面支持等级、时间类型、来源、类型、原因和关键词筛选；JSON API 和 CSV 导出必须和页面使用同一筛选语义，并避免筛选请求为了 total 重复全量扫描；超长或非法的精确筛选参数必须返回 `400 invalid_filter`，不得截断后匹配。
- `epochSec` 或当前 boot 可解析时显示真实时间；不可解析时显示 `uptime N ms` 和 `boot N`，不得伪造日期。
- Web 页面、JSON API 和 CSV 导出分别验证 HTML/JSON/CSV escape；CSV 文本字段还必须防护 `= + - @` 等 spreadsheet formula 前缀。
- CSV 导出读取失败时必须输出可见错误行或错误状态，不得静默返回截断内容。
- App Events 成功 `begin/append/read/clear` 后不得保留旧 `lastError()`；只有当前失败或本次读取确实跳过损坏记录时才展示诊断文本。
- `POST /esp32base/tools/app-events-clear` 必须需要认证、POST 和同源检查；GET 不得触发清空，App Events 页面本身不得放清空按钮。
- `GET /esp32base/app-events`、JSON API 和 CSV 导出不得隐式创建或重建 `/esp32base/app-events/events.bin`；App Events 未 ready 时应展示 unavailable/fault。
- `/esp32base/logs` 不显示 App Events，App Events 不混入 WiFi、OTA、NTP、启动、健康状态等系统诊断日志。
- 应用接入验收时必须检查 App Events 不重复记录 Esp32Base 系统事件，例如 boot/reset/restart reason、WiFi、NTP、OTA、LittleFS mount/write fault、FileLog fault 或基础库健康状态；同一故障如果两边都记录，系统诊断日志必须表达技术原因和内部错误链路，App Event 必须表达业务影响、保护动作、跳过原因、用户维护结果或外部决策结果。
- `examples/app_events_demo` 可构建，并能演示写入、分页读取、Web/API 展示和清空。

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
- WiFi 配置页回显 SSID，不回显密码；空密码字段保留已保存密码，显式清空用于开放 WiFi。
- WiFi 凭据保存失败时不启动新的运行中连接尝试；保存成功路径需要读回校验 SSID/password。
- 重启按钮二次确认。
- OTA 上传进度显示。
- 路由器掉电 5 分钟后恢复。
- 已保存凭证但连接失败时不进入 AP，持续重连并 backoff。
- NVS 写满。
- LittleFS 首次挂载失败不 halt。
- LittleFS 二进制文件读写和目录列举；文件逻辑大小存在但内容不可读时，FS API 应返回失败并由 `/esp32base/fs` 标记 `unreadable`。只读模式不提供下载，管理模式仍允许单文件删除，便于清理损坏文件。
- FS 满或损坏时触发 Web WARN 诊断，不应因为 FileLog 写入失败导致 task WDT 重启。
- FileLog 默认 `4 × 32KB` 轮转。
- FileLog mode 开启但 FS/init 前置条件不满足时，Web 应显示 `unavailable`，不能误显示为用户关闭的 `disabled`。
- System Logs 页面可读取 history/current。
- System Logs 页面和 raw endpoint 的 GET 读取必须是只读快照，不主动 `flush()` 或改变 FileLog 运行态；清空、格式化、重启等维护副作用必须走 POST。
- System Logs 页面清空后可继续写 current。
- App Events 启用后默认约 188 KiB 存储、分页读取、常用筛选、真实时间/uptime fallback、CSV 导出、POST 清空和环形覆盖正常；未启用时无 App Events 路由和符号依赖。
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
- Health tick 未超过 loop 阈值时，默认每 30 分钟最多输出 1 条 DEBUG 日志；DEBUG 不进入系统诊断日志模式。
- Health tick 窗口内最大 loop 间隔超过 `ESP32BASE_HEALTH_LOOP_WARN_MS` 时输出 INFO。
- `ESP32BASE_HEALTH_LOOP_WARN_MS` 默认 3000ms；高实时性业务可自行降到 1000/2000ms，长操作较多的应用可升到 5000ms。
- `ESP32BASE_HEALTH_DEBUG_LOG_INTERVAL_MS` 默认 1800000ms；设为 0 可关闭普通 DEBUG tick 日志，不影响 INFO 慢循环日志。
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
- `boot_count` 是 `uint32_t` 重启计数，上电、手动复位、`ESP.restart()`、Watchdog、OTA 重启等会清除普通内存的非 sleep 重启计入；deep sleep 唤醒不增加 boot_count，也不为 `boot_cnt` 写 NVS；不能把它解释为单纯上电次数或每次唤醒次数。
- `reset_reason` 表示本次复位/启动来源；`wake_reason` 仅表示 sleep 唤醒来源，普通上电或普通复位时为 `undefined` 是正常状态；`reset_desc` / `wake_desc` 输出中文简明解释。
- restart/deep sleep 生命周期 reason 仍由 `Esp32BaseSystem::appendRestartLog()` / `restartLogCount()` 记录，与 `boot_count` 分开看。
- 启动日志按 begin 顺序输出，便于定位失败模块。
- `DEBUG` 编译级别下，启动流程输出模块初始化顺序，以及 Web/NTP/mDNS/OTA 延迟启动或停止原因；这些日志用于开发诊断，默认 `INFO` 构建不输出。
- NTP 对时前日志时间戳使用启动后毫秒数，例如 `[42442]`；对时后切换为绝对日期时间。
- NTP 默认按 UTC+8 输出本地时间，应用可通过 `ESP32BASE_NTP_GMT_OFFSET_SEC` / `ESP32BASE_NTP_DAYLIGHT_OFFSET_SEC` 覆盖。
- NTP 同步判断默认要求 epoch >= `ESP32BASE_NTP_SYNC_MIN_EPOCH`，默认值为 `1700000000UL`；本 boot 首次可信同步后会锁存时间映射，后续不要求 SNTP status 持续保持 completed。
- NTP 未同步状态不输出周期性 WARN/DEBUG；只有明确的单次同步失败事件才应输出 WARN。
- NTP profile 启动时复用系统 boot count 作为 `bootId`，日志输出 `time_boot_session boot_id=N source=boot_count`；NTP 不再为 boot session 额外写启动期 NVS。deep sleep 唤醒沿用上一次非 sleep 重启计数；业务离线事件应同时保存 bootId 和 uptimeSec，不要把 bootId 当作每次 deep sleep 唤醒的唯一会话号。
- 日志和人工页面中的字节数只显示 KB/MB/B 人性化值；状态/API JSON 可保留 raw `bytes` 并附带 `human`。
- NTP 对时成功后输出实际时间、当前 uptime、推算 boot wall time，并让 `TimeSnapshot.synced`、Status 页 NTP time 行和日志绝对时间切换共享同一可信同步语义。
- WiFi 连接、断开、重连 backoff、进入/退出 config portal 都必须有清晰日志。慢速 backoff 阶段的重复 WiFi 重试日志使用 INFO，避免长期离线设备在默认 WARN FileLog 下持续写 Flash；首次故障和短间隔重试仍保留 WARN。
- WiFi 初始化安全启动保护必须输出可诊断日志：`wifi_init_safe_boot_guard_set` 表示已在任何 Arduino WiFi 初始化调用前建立 guard，`wifi_init_safe_boot_guarded_reset` 表示检测到连续 WiFi 初始化期 brownout/panic/watchdog/software reset 但仍未达到阈值，`wifi_init_safe_boot_pause` 表示已暂停 WiFi 初始化并进入无 WiFi 诊断状态，`wifi_init_safe_boot_paused` 表示危险复位后仍保持暂停，`wifi_init_safe_boot_auto_resume` 表示后续非危险复位自动恢复一次 WiFi 初始化。触发暂停时必须输出中文供电风险提示，说明疑似 WiFi/RF 启动瞬时电流导致供电跌落，并建议检查电源、USB 线、稳压器余量、接线和板端 VIN/5V-GND 低 ESR 储能电容。
- WiFi STA 安全启动保护必须输出可诊断日志：`sta_safe_boot_guard_set` 表示已进入 guarded STA 尝试，`sta_safe_boot_guarded_reset` 表示检测到连续 guarded brownout/panic/watchdog 复位但仍未达到阈值，`sta_safe_boot_pause` 表示已暂停凭据并回退 AP 配网，`sta_safe_boot_paused` 表示危险复位后仍保持暂停，`sta_safe_boot_auto_resume` 表示后续非危险复位自动恢复已保存 STA 尝试，`sta_safe_boot_resume` 表示用户重新提交凭据或点击重试后恢复 STA 尝试，`sta_safe_boot_cleared` 表示连接成功、凭据清除或恢复重试后保护状态已清空。
- 系统诊断日志默认 WARN；量产极简现场记录可通过 Web System 页或 `Esp32BaseFileLog::setMode(Esp32BaseFileLog::ERROR)` 只记录 ERROR；现场调试可切到 INFO，方便通过 System Logs 页面观察。INFO 模式会把启动、联网、OTA、性能提示和维护动作等低优先级日志写入 LittleFS，不做节流，不建议作为长期量产默认。
- FileLog 模式变更必须输出 WARN 级审计日志，包含上一次模式和新模式。
- Serial 日志和 FileLog 必须可独立控制；量产设备可通过 `Esp32BaseLog::setSerialLevel(Esp32BaseLog::NONE)` 关闭串口，同时保持 FileLog WARN/ERROR 或 INFO 正常写入。
- `ESP32BASE_LOG_LEVEL` 是编译期上限；如果编译为 `ESP32BASE_LOG_NONE`，日志宏被移除，FileLog 也不会收到日志。
- 普通页面 GET 慢请求、认证成功、路由注册和配置审计 skipped/read/deferred 属于 DEBUG 诊断；POST 等操作慢请求和健康慢循环属于 INFO 性能提示；配置实际写入、flush、启动认证加载、WiFi/OTA/NTP/mDNS 状态、格式化、重启和认证失败应保留 INFO/WARN/ERROR。
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
- App Events append/header/record/clear 失败，必须通过 `lastError()` 和 Web fault notice 可见。
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
