# 已知限制

本文集中列出 `Esp32Base` 当前明确接受的边界。它们不是待补功能，而是为保持库轻量、可裁剪、可维护而刻意限定的范围。

## 1. 安全边界

- Web 使用 HTTP Basic Auth，不提供 HTTPS。
- Basic Auth 明文传输，只用于降低误操作风险，不抵御同一局域网内的嗅探、MITM 或主动攻击。
- Web/Auth 不再内置启用 admin/admin。未调用 `Esp32BaseWeb::setDefaultAuth()` 且没有已保存认证时，Web 服务不会启动；仅显式启用 `ESP32BASE_WEB_ALLOW_INSECURE_DEFAULT_AUTH=1` 的受控开发固件会使用内置 `admin/admin` 兜底。
- Web Auth 密码不在 HTML、JSON、API 响应或日志中输出；日志只输出用户名、来源、结果和 `password_set` 状态。
- WiFi 凭据和 Web Auth 凭据保存到普通 NVS；未启用芯片/平台级 flash encryption 时，具备物理 flash 读取能力的人可以取得明文凭据。日志不输出密码值，只输出 SSID、用户名、来源、结果或 `password_set` 这类状态。
- OTA 只提供上传认证和完整性校验，不提供固件加密、签名信任链或差分升级；Web OTA 使用可选 SHA256，ArduinoOTA/espota 使用内建 MD5。
- Web OTA 与 Web Auth 配置解耦；关闭 Web Auth 时 Web OTA 仍可访问，但没有密码保护，风险由应用和用户自行承担。
- 关闭 Web Auth 不会关闭 ArduinoOTA/espota 密码；命令行 OTA 仍要求当前 Web Auth 密码。
- 内置危险 POST 使用 POST method、Web Auth 和轻量 `Origin` / `Referer` 同源检查，不提供完整 CSRF token；缺少 Origin/Referer 的请求会继续放行，以兼容 curl、PlatformIO `webota` 和简单脚本。
- Web 配置页面不会回显当前 WiFi 密码；留空表示保持已保存密码，显式勾选清除密码才会切换为开放网络。
- 长操作不再从 task WDT 注销当前任务；Esp32BaseFs 大块读写会分块 feed/yield，但不可细分的底层调用仍可能触发 WDT，这是保留卡死兜底的预期行为。
- 内置诊断页面和状态 API 会输出 hostname、MAC、heap、flash、分区、reset/wake reason、OTA 和文件系统等设备指纹信息；当前不做 rate limit，Web Auth 只提供访问门槛，不提供抗扫描或滥用保护。

## 2. Web 边界

- Web 基于 Arduino `WebServer` 同步模型。
- 长时间 handler 会阻塞其他请求，建议单次 handler < 200ms。
- 不支持 WebSocket、SPA、大型前端资源管理器、多用户权限和会话系统。
- System Logs 的 GET 页面只读取已落盘的系统诊断日志快照，不为了“立即看到缓存中日志”主动 flush；低优先级缓存日志按常规 flush interval 落盘，清空、格式化、重启等维护动作仍必须走 POST。
- System Logs 的 `unavailable` 表示模式开启但当前 FS/init 前置条件无法启用系统诊断日志；`disabled` 才表示用户把模式设为 OFF。
- OTA 上传页只复用 Web Basic Auth，不提供第二套认证。
- ArduinoOTA/espota 不提供用户名，认证只使用当前 Web Auth 密码。

## 3. 网络边界

- 有已保存 WiFi 凭证但普通连接失败时，库不会自动进入 AP/config portal，而是持续 STA 重连。
- WiFi 初始化安全启动保护是更早的保护层：如果设备连续在 Arduino WiFi 初始化最早期发生 guarded brownout、panic、watchdog 或 software reset，达到阈值后会暂停 WiFi 初始化并进入无 WiFi 诊断状态，而不是继续尝试 AP/config portal。日志会用中文提示疑似 WiFi/RF 启动瞬时电流导致供电跌落，建议检查供电链路并考虑在板端 VIN/5V 与 GND 间增加低 ESR 储能电容。该提示不是对“电容不足”的唯一归因，仍需排查电源限流、USB 线压降、稳压器余量和接线接触电阻。
- STA 安全启动保护是例外：如果上一次已进入 guarded STA 启动阶段，随后连续发生 brownout、panic 或 watchdog 类复位，达到阈值后会暂停 `eb_wifi` 凭据并回退 AP/config portal。后续正常 `poweron`、外部复位或其他非危险复位会自动恢复一次已保存 STA 尝试；Web WiFi 页面也提供重试已保存凭据入口，不要求重新保存同一组密码。该机制用于跳出坏 STA 状态造成的永久重启循环，不用于处理路由器临时离线。
- 进入 AP/config portal 只发生在无凭证、显式 `startConfigPortal()`、STA 安全启动保护触发或应用自定义策略下。
- Config portal AP 默认不设置密码，SSID 为可预测的 `ESP32-Config-XXXX`，其中后缀来自 eFuse MAC 的最后两个字节；应用应只在预期配网窗口进入 portal。
- 该策略用于防止量产设备在路由器临时故障时被陌生人通过 AP 修改凭证。
- 如果应用面向消费场景，需要换路由器后自动配网，应由应用在长 backoff 后显式调用 `Esp32BaseWiFi::startConfigPortal()`，并配合按键长按、状态灯或屏幕提示等用户确认方式。
- deep sleep 期间 STA、AP、DNS、Web 均不可访问；唤醒后按新启动流程恢复。
- Captive Portal DNS 对所有查询返回 AP IP，不提供按域名扩展的 `addCaptiveTarget()`。

## 4. 时间和 RTC 边界

- `Esp32BaseTime` 是业务侧统一时间入口；`Esp32BaseNtp` 只表示 NTP 客户端状态，不作为 RTC-only 或离线设备的通用真实时间 API。
- 外部 RTC 只作为可选可信时间源，不随任何 profile 自动启用。应用固件必须在构建期配置 `ESP32BASE_ENABLE_RTC=1`，并通过 `ESP32BASE_RTC_DRIVER` 在 DS3231 / PCF8563 中二选一；基础库不做运行时自动识别，也不支持同一固件同时接管两个 RTC 时间源。
- RTC 芯片寄存器按 UTC 日历字段读写；显示、日志和 Web 页面通过 `ESP32BASE_NTP_GMT_OFFSET_SEC` / `ESP32BASE_NTP_DAYLIGHT_OFFSET_SEC` 做固定偏移格式化。基础库不提供时区数据库、夏令时规则或按地区自动切换。
- RTC 缺失、I2C 读写失败、振荡停止、低电压或时间低于可信 epoch 下限都不会让 `Esp32Base::begin()` 失败；这些情况只作为诊断状态暴露。需要把 RTC 异常提升为业务故障、停机或告警时，由应用层按自身策略处理。
- 基础库只使用 RTC 的低频时间读写、状态缓存、Web Status 展示和 NTP 成功后的可选写回。DS3231 / PCF8563 的闹钟、中断引脚、INT/SQW、方波、定时器、温度、校准等扩展能力不由基础库接管。
- 业务项目可以继续在同一 I2C 总线上自行访问 RTC 芯片扩展寄存器或使用外部 RTC 库，但必须自行负责寄存器语义、并发时序和避免整寄存器写入误清状态位。若业务直接改写 RTC 时间，应在 loop/system task 中调用 `Esp32BaseRtc::refresh()` 让基础库缓存重新收敛。
- `Esp32BaseTime::snapshot()` 不会每次读取 RTC；默认启动时读取一次，NTP 写回时按阈值写一次，周期健康刷新需要显式设置 `ESP32BASE_RTC_STATUS_REFRESH_MS`。基础库不以高频 I2C 轮询作为时间来源。

## 5. 并发边界

- `Esp32Base::begin()`、`Esp32Base::handle()`、`Esp32BaseConfig`、`Esp32BaseWeb` 和 `Esp32BaseBus` 按单系统服务任务模型使用，推荐固定在 Arduino `loopTask` 或应用自建的同一个服务任务中调用。
- `Esp32BaseConfig` 当前不是线程安全 API，不提供内部 mutex；业务 FreeRTOS task 不应跨核直接调用 Config 写入、flush 或清理 API。
- 双核项目推荐架构是：WiFi/Web/OTA/NVS/FileLog 等基础库服务运行在同一个 loop/system task，实时或核心业务 task 通过 FreeRTOS queue、flag 或 ring buffer 投递请求。
- 实时任务不要直接做 NVS、LittleFS、Web、OTA、FileLog 操作，避免 flash 写入、文件系统和网络栈阻塞实时响应。
- `Esp32BaseBus::publish()` 只允许在 Arduino loop 任务调用。
- WiFi callback、timer callback、ISR、LWIP 回调不得直接 publish，只能入队后在 `handle()` 中处理。
- 本库不提供跨任务事件总线，不包装 FreeRTOS 队列作为公开通用消息系统。
- `Esp32BaseRecordStore` 和 `Esp32BaseAppEvents` 同样不是线程安全或回调可重入API；其他任务必须把追加、读取、reload和清空请求投递到同一system/loop任务。

## 6. RecordStore 边界

- 每个Store只支持一种固定长度负载，不支持可变记录、字段查询、索引、事务、更新或单条删除。
- ID只在单个“记录类型 + storeVersion”文件内唯一；不同文件可以出现相同ID。32位ID到达上限后停止追加，不回卷。
- 文件创建后不自动扩缩容。改变负载或容量时使用新storeVersion，或由用户明确清理旧文件后重建。
- 清空是header控制的逻辑清空，不会立即覆盖旧槽位，不提供安全擦除保证。
- 启动会扫描配置容量内的全部槽位；业务应根据嵌入式设备实际保留需求设置合理预算，不把它当成无限数据库。
- 追加是同步持久化操作，包含LittleFS flush和写后验证，延迟取决于芯片、Core、分区及文件大小。经典ESP32、Arduino Core 2.0.16、100 KiB App Events文件的实机测量约为1.1秒/条，主要耗时在LittleFS flush；这不适合实时控制路径，业务应通过队列交给system/loop任务。基础库保留flush以维持明确的成功与断电恢复语义，不为这一延迟引入分段文件或异步事务层。
- 动作过程中断电且尚未调用`appendCompleted()`时，不存在可恢复的完成记录。需要持久化“进行中动作”的业务必须自行设计恢复状态。

## 7. 存储边界

- Config 后端为 ESP32 NVS，只适合小配置，不适合大量数据或高频日志。
- Config 字符串 API 支持最大 3999 字节可见内容，并使用固定 scratch buffer 读取和比较，避免 Arduino `String` 造成 heap 碎片；它仍然不适合大量数据或高频日志。
- Config blob / POD API 支持最大 256 字节，只适合 head/count/next_id 这类小型固定布局元数据；业务记录正文、事件环形文件和高频日志仍应放在文件系统或业务自己的存储结构中。
- NVS 写满时 set API 返回 false，库不自动删除业务数据。
- `clearLibraryNamespaces()` 只清理 `eb_` 前缀的库 namespace。
- 应用不得使用 `eb_` 前缀作为自己的 NVS namespace。
- LittleFS 默认不自动格式化；首次空分区 mount failed 不会 halt。格式化 LittleFS 只能来自明确维护动作，因为该操作会清空当前 LittleFS 分区全部文件。

App Events 边界：

- App Events 是基于 RecordStore 的固定容量事件记录，适合低频关键业务事件；完整的浇水、开关门、喂食历史应使用各自的业务 RecordStore。
- 用水量统计、报表数据、累计量、传感器采样历史、完整执行历史、大 payload、高频明细和不允许覆盖的业务数据，应由业务自己的数据文件或存储结构负责。
- `/esp32base/records/**` 是基础库管理的 RecordStore 容器；FS 管理页允许下载检查，但不允许上传、覆盖或删除，以免绕过结构和完整性约束。App Events 只能通过 System 页取得用户确认后逻辑清空；业务 RecordStore 的清空确认和入口由业务项目负责。
- App Events 也不是第二套系统诊断日志。boot/reset/restart reason、WiFi、NTP、OTA、LittleFS、FileLog fault、基础库健康状态等 Esp32Base 系统事件仍归 Status、System diagnostics 和 FileLog；App Events 只记录应用业务决策或业务可解释事件。同一故障可同时有系统诊断日志和 App Event，但前者写技术事实和内部错误链路，后者写业务影响、保护动作、跳过原因、用户维护结果或外部决策结果。

## 8. 文件系统边界

- LittleFS 用于小型配置文件、诊断文件和 Web 静态小资源。
- 系统诊断日志默认位于 `/esp32base/logs/system.log`，App Events store 默认位于 `/esp32base/records/app-events.v1.bin`；业务 RecordStore 也位于 `/esp32base/records/**`，其他业务文件应放在 `/app/**`、`/data/**` 或项目自定义目录。
- 业务需要二进制定长日志时，应通过 `Esp32BaseFs::readBytesAt()` / `writeBytesAt()` 做分页读取和固定位置覆盖，不直接依赖 LittleFS 或 Arduino `File`。
- `writeBytesAt()` 只覆盖已有文件内容，不创建、不扩展文件；环形文件容量应由 `createFixedFile()` 初始化或校验。
- `createFixedFile()` 会在文件不存在、大小不匹配或同尺寸但末端不可读时重建固定大小文件；同尺寸且可读时保留原内容，避免启动期清空持久环形记录。`appendBytes()` 适合低频追加，不适合循环零填充大文件。
- 大块读写会在 Esp32BaseFs 层分块并定期让出调度，降低同步 LittleFS I/O 触发 task watchdog 的风险；但 LittleFS/Web/OTA/FileLog 仍是同步阻塞操作，实时任务仍应通过队列投递给 loop/system task 处理。
- 写入失败不等于已回滚；FS 满、电源异常或底层写失败后，文件可能部分更新、大小不符或内容不可读，业务必须检查返回值并按业务语义重试、丢弃暂存或提示维护。
- 逻辑文件大小不等于内容一定可读；brownout、FS 满、跨项目反复烧录或业务侧未处理写失败后，LittleFS 可能仍能列出文件大小，但读取内容失败。`Esp32BaseFs` 会把这种“未到 EOF 却读出 0 字节”的情况返回为失败，业务必须检查返回值。
- 不提供大型文件管理器。
- 不保证在 OTA 写 flash 时并行执行大量 FS 写入的实时性。
- Fs 没有 maintenance handle；挂载后按显式 API 操作。

## 9. 资源边界

- 当前版本不依赖 PSRAM。
- 即使板子有 PSRAM，默认资源预算也按无 PSRAM 设计。
- ESP32-C3 单核、内存和 wake source 更受限，部分模块默认容量更保守；Web route 默认仍统一为 24，应用可按自身 route 数量显式调小。
- FULL profile 必须通过实机容量验证确认，不能只看依赖声明。

## 10. 兼容边界

- 支持 Arduino ESP32 Core 2.0.14+ 和 3.0.4+。
- Watchdog、WiFi event、mDNS、brownout 控制必须使用版本条件编译隔离。
- mDNS `stop()` 对外语义是停止广告，不承诺释放底层 mDNS 全部资源。
- ESP32-C3 不支持的 wake source 返回 false 并输出 warn。

## 11. OTA 边界

- OTA 只支持整包升级。
- 未提供 SHA256 时允许跳过完整性校验；提供时必须严格校验。
- SHA256 校验失败不得调用 `Update.end(true)`，不得重启到新固件。
- SHA256 规则仅适用于 Web OTA；espota 继续使用 ArduinoOTA 协议内建 MD5。
- 默认不关闭 brownout detector；临时关闭 brownout 仅作为显式开启的风险选项。

## 12. 日志边界

- Core Log 只输出 Serial 和可选 sink callback，不依赖 FS。
- 系统诊断日志由 Runtime/FS 的 `Esp32BaseFileLog` 提供；CORE 和默认 NET 不具备该能力。FileLog 是实现/API 名称，业务文档和页面默认称为 System Logs 或系统诊断日志。
- `INFO` 日志可输出 WiFi SSID、Web Auth 用户名、来源、结果和 `password_set` 状态；不得输出 WiFi 或 Web Auth 密码值。
- 日志访问权限由应用和部署环境控制。
- 用户 sink callback 同步执行，不得长时间阻塞。
