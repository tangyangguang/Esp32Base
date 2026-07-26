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

- MQTT 3.1.1 Client 是显式可选能力，只支持单 Broker、单 Client、Clean Session、QoS 0/1、TCP/TLS；不支持 MQTT 5、QoS 2、持久会话恢复、多 Broker、WebSocket、云厂商 SDK、设备影子或远程 MQTT OTA。
- MQTTS 必须由应用提供有效 CA，并等待 `Esp32BaseTime` 可信时间；不提供跳过证书或校验失败后回落明文。系统 CA bundle 当前不公开。
- MQTT 用户名、密码、CA、客户端证书、私钥、LWT、订阅 Topic 按借用内存处理，必须保持到设备重启或进入 deep sleep；基础库不持久化、不提供 Web 配置页，也不提供安全擦除保证。
- MQTT publish 不构成业务离线队列。仅已连接时接受；QoS 1 断线未 ACK 报文报告 `EVENT_PUBLISH_DELIVERY_UNCERTAIN`，其后仍可能被 ESP-MQTT 的有界协议 outbox 重传，不能据此断言“确定未送达”。业务需要幂等，并在重连后重新发布当前状态。
- QoS 0 无 Broker ACK，基础库不能承诺 Broker 已收到。QoS 1 ACK 也不等于业务已处理；重复和应用幂等按 MQTT/业务契约处理。
- ESP-MQTT 创建独立 FreeRTOS task；业务 callback 仍只在 `Esp32Base::handle()` 所在任务执行。`configure()`、订阅、publish 和 callback 注册不是任意跨任务 API。
- Arduino Core 预编译 ESP-MQTT 基本同时带入 TLS transport，MQTT 开启后的明文构建不保证明显节省 Flash。真实 TLS heap 峰值、modem sleep Keepalive 和长稳必须按产品实机验证。
- MQTT 连接拒绝能稳定区分协议、Client ID、用户名和授权；DNS、socket 和部分 TLS 错误在 Core 版本间只保证稳定大类，详细原因通过 native code 诊断。

- 有已保存 WiFi 凭证但普通连接失败时，库不会自动进入 AP/config portal，而是持续 STA 重连。
- WiFi 初始化安全启动保护是更早的保护层：如果设备连续在 Arduino WiFi 初始化最早期发生 guarded brownout、panic、watchdog 或 software reset，达到阈值后会暂停 WiFi 初始化并进入无 WiFi 诊断状态，而不是继续尝试 AP/config portal。日志会用中文提示疑似 WiFi/RF 启动瞬时电流导致供电跌落，建议检查供电链路并考虑在板端 VIN/5V 与 GND 间增加低 ESR 储能电容。该提示不是对“电容不足”的唯一归因，仍需排查电源限流、USB 线压降、稳压器余量和接线接触电阻。
- STA 安全启动保护是例外：如果上一次已进入 guarded STA 启动阶段，随后连续发生 brownout、panic 或 watchdog 类复位，达到阈值后会暂停 `eb_wifi` 凭据并回退 AP/config portal。后续正常 `poweron`、外部复位或其他非危险复位会自动恢复一次已保存 STA 尝试；Web WiFi 页面也提供重试已保存凭据入口，不要求重新保存同一组密码。该机制用于跳出坏 STA 状态造成的永久重启循环，不用于处理路由器临时离线。
- 进入 AP/config portal 只发生在无凭证、恢复按键、显式 `startConfigPortal()`、STA 安全启动保护触发或应用自定义策略下。
- Config portal AP 默认不设置密码，SSID 为可预测的 `ESP32-Config-XXXX`，其中后缀来自 eFuse MAC 的最后两个字节；应用应只在预期配网窗口进入 portal。
- 该策略用于防止量产设备在路由器临时故障时被陌生人通过 AP 修改凭证。
- 启用WiFi时默认提供物理恢复热点按键：ESP32 / ESP32-S3为GPIO0，ESP32-C3为GPIO9，应用运行后长按10秒进入配置热点且保留凭据。默认键也是芯片BOOT strapping键，复位或上电期间保持按下会进入ROM下载模式，应用层无法改变。System页可关闭或改GPIO/时间；基础库只能拒绝无效和已知Flash GPIO，无法检测具体板卡上的外设、PSRAM、USB或业务引脚冲突，改动前必须核对原理图。deep sleep期间`handle()`不运行，因此按键不检测；需要从deep sleep唤醒的产品由业务配置独立wake source。
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
- ID只在单个“记录类型 + storeVersion”目录内唯一；不同Store可以出现相同ID。32位ID到达上限后停止追加，不回卷。
- `maximumStoreBytes` 是最大逻辑预算，不预分配大文件；调大预算可在下次 `begin()`/`reload()` 生效，改变payload长度或字段版本应使用新storeVersion。
- 清空由双控制头提交逻辑可见边界，再尽力删除旧段，不提供安全擦除保证。
- 启动会扫描当前有限段内的全部候选槽位；业务应根据嵌入式设备实际保留需求设置合理预算，不把它当成无限数据库。
- 追加是同步持久化操作，包含LittleFS flush和写后验证。经典ESP32、Arduino Core 2.0.16实测：旧100KiB单文件同步覆盖约1.08～1.11秒；当前分段方案在100KiB～1MiB总预算下普通追加P50约46～54ms，但P95/最大值仍可达数百毫秒或偶发超过1秒。这不适合实时控制路径，业务应通过队列交给system/loop任务。详细条件和完整数据见 [Record Store 设计、接入与实机基准](12_record_store.md)。
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
- `/esp32base/records/**` 是基础库管理的 RecordStore 容器；FS 管理页允许下载检查，但不允许上传、覆盖或删除，以免绕过结构和完整性约束。App Events 通过自己的 System 危险操作逻辑清空；当前业务 RecordStore 在应用显式调用 `Esp32BaseWeb::registerBusinessRecordStore()` 后，由 System 页统一预检并逻辑清空。未登记历史版本不在该操作范围内；其目录级删除需要独立、安全的文件维护流程，当前 FS 管理页尚不提供。
- App Events 也不是第二套系统诊断日志。boot/reset/restart reason、WiFi、NTP、OTA、LittleFS、FileLog fault、基础库健康状态等 Esp32Base 系统事件仍归 Status、System diagnostics 和 FileLog；App Events 只记录应用业务决策或业务可解释事件。同一故障可同时有系统诊断日志和 App Event，但前者写技术事实和内部错误链路，后者写业务影响、保护动作、跳过原因、用户维护结果或外部决策结果。
- `appendDiscreteEvent()` 不去重；调用方把周期故障误当离散事件反复提交，仍会产生事件风暴。周期状态必须使用 `observeConditionState()`，方法名和返回枚举用于显式区分两种语义。
- `conditionId` 是持久化位图schema，不是可以随固件版本任意复用的临时编号。保留 `eb_app_events` NVS 的升级必须维持ID含义；需要改变映射时由应用安排显式状态清理或迁移。
- 条件确认依赖应用继续调用 `observeConditionState()`，基础库不调度下一次硬件访问。确认时间最大为 `INT32_MAX` 毫秒；正常秒/分钟级配置可跨越约49.7天的 `millis()` 回绕。已经确认且长期不恢复的条件不继续计时，因此不会在49天后失效。
- 条件事件先提交LittleFS，再更新 `eb_app_events.active_id_bits`。若事件写入成功后、NVS提交前掉电，重启后可能额外产生一次相同转换；基础库不为消除这一极小窗口增加事务日志。NVS写失败会显式返回部分失败并阻止后续条件转换，直到 `reload()` 保存成功。
- `factoryReset()`清除条件NVS但不反向调用Runtime。若RAM中正有事件已写而条件状态待保存，随后调用`reload()`会优先重试该状态并重新写入NVS；完整出厂重置应在清理成功后重启，不能在这一失败状态下用`reload()`代替重启。

## 8. 文件系统边界

- LittleFS 用于小型配置文件、诊断文件和 Web 静态小资源。
- 系统诊断日志默认位于 `/esp32base/logs/system.log`，App Events store 默认位于 `/esp32base/records/app-events.v2/`；业务 RecordStore 也位于 `/esp32base/records/**`，其他业务文件应放在 `/app/**`、`/data/**` 或项目自定义目录。
- 业务需要可靠、轮换、分页的固定长度历史时应优先使用 `Esp32BaseRecordStore`；只有其语义不适用时才直接使用 `Esp32BaseFs` 设计自己的文件结构。
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
