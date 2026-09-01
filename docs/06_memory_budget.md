# 内存与容量预算

## 1. 原则

本库的资源承诺按 profile 评估。

不能只看 `library.json` 依赖元数据，必须以编译产物为准：

- map 文件。
- `text / data / bss`。
- firmware size。
- 实机 free heap。
- min free heap。

## 2. 编译期裁剪验收

必须满足：

| Profile | 必须链接 | 不应链接 |
| --- | --- | --- |
| MINIMAL | Config, System | WiFi, WebServer, Update, LittleFS, MQTT |
| OFFLINE | LittleFS, FileLog, Watchdog, Health | WiFi, WebServer, Update, MQTT |
| LOCAL | WiFi, WebServer, Update | MQTT |
| IOT | WiFi, WebServer, Update, MQTT | 无Profile外强制能力 |

四种Profile都不得链接ArduinoOTA/espota符号。

关闭模块要求：

- 无对应可选模块 `.inc` 被 `Esp32Base.cpp` 包含。
- 无对应全局对象。
- 无对应静态表。
- 无对应符号进入 map。

Status 页不创建后台任务、定时器、历史采样缓存或自动刷新。页面请求时读取已有内存状态、网络参数、NVS 条目摘要和固件摘要；当前镜像大小每次启动最多读取一次镜像元数据并缓存，不进行全镜像校验、完整分区遍历或文件树扫描。页面关闭后没有持续 CPU、RAM 分配或 Flash 写入成本。

## 3. 默认容量

### 3.1 Bus

```cpp
#if defined(CONFIG_IDF_TARGET_ESP32C3)
#define ESP32BASE_BUS_MAX_SUBSCRIBERS 12
#else
#define ESP32BASE_BUS_MAX_SUBSCRIBERS 16
#endif
```

### 3.2 Web route

```cpp
#define ESP32BASE_WEB_MAX_ROUTES 24
```

该上限只控制应用通过 `addRoute()` / `addPage()` / `addApi()` 注册的静态 route 槽位；内置 Web 路由不占用此表。默认24是基础库当前LOCAL/IOT的设计目标，优先避免业务页面/API 增长时过早触发 route 容量不足。route 较少且需要节省静态 RAM 的应用，可以在构建参数里显式调小。

### 3.3 Config pending

默认：

```cpp
#define ESP32BASE_EB_CONFIG_PENDING_MAX 8
#define ESP32BASE_CONFIG_PENDING_MAX ESP32BASE_EB_CONFIG_PENDING_MAX
```

每个 pending blob 按实际长度动态分配，单个 blob 最大 256 字节。Config 只保留一个 256 字节静态 scratch buffer，用于 blob 和短字符串的写前比较；字符串读取优先直接写入调用方缓冲，调用方缓冲不足或长字符串需要写前比较时，才在当前调用期间按 NVS 实际长度临时分配，完成后立即释放。MINIMAL不再为最大3999字节字符串常驻预留4000字节RAM。

### 3.4 FileLog

系统诊断日志仅在 FS profile 中启用，底层实现/API 名称是 `Esp32BaseFileLog`。默认路径为 `/esp32base/logs/system.log`，默认 `4 × 32KB = 128KB`，低优先级缓存 1KB，flush interval 2s。默认模式 ERROR，用于让全功能固件在无业务数据或显式调试需求时保持很低的 Flash 写入量；现场排查可显式切到 WARN 或 INFO。MINIMAL不链接LittleFS，也不产生FileLog静态状态。

### 3.5 App Config

App Config 注册字符串和 enum option 数组只保存指针，不按 label、help 或 unit 的最大长度为每个字段预留 RAM，也不把这些显示文字写入 NVS 或 LittleFS。help 最大 192 个 UTF-8 字节；提高或使用该上限不会扩大固定 512 字节 Web 流式输出缓冲。固件 Flash 和页面传输量只按业务实际提供的文字长度增加，注册时的长度校验和页面 HTML 转义时间也随实际长度线性增加。

### 3.6 Storage、RecordStore 与 Conditions

Storage协调层固定保存最多8个Store指针、合计预算和少量状态，不复制payload或记录内容。默认业务Store合计预算上限为512 KiB，FileLog默认预算128 KiB，并保留 `max(128 KiB, LittleFS/4)` 安全余量。具体容量规划见 [Record Store与统一存储协调](12_record_store.md)。

RecordStore逻辑占用为 `128字节控制文件 + 各段32字节段头 + 记录数 × (payloadSizeBytes + 24)`。分段按预算和槽位自适应选择8/16/32/64 KiB级别，常见32～512 KiB Store通常维持不超过16个段文件。

Conditions默认关闭，仅在 `ESP32BASE_ENABLE_CONDITIONS=1` 时编译。库内常驻活动ID位图、已登记ID位图和少量加载状态；应用每实际条件长期持有一个不动态分配的 `ConditionTracker`。NVS只保存 `eb_conditions.active_bits` 一个 `uint32_t`。普通观察、确认等待、Unknown和状态不变不写NVS或LittleFS；确认转换只更新该整数，Conditions本身不保存历史。

### 3.7 Health

```cpp
ESP32BASE_HEALTH_TICK_INTERVAL_MS=30000
ESP32BASE_HEALTH_DEBUG_LOG_INTERVAL_MS=1800000
```

### 3.8 Restart log

Restart log 固定保留4个槽位，当前不提供容量覆盖宏。

## 4. 字符串和缓冲

默认限制：

- hostname：32 字节。
- firmware name：32 字节。
- firmware version：16 字节。
- firmware build：32 字节。
- WiFi SSID：33 字节。
- WiFi password：65 字节。
- NVS namespace：15 字符。
- NVS key：15 字符。
- NVS string value：可见内容 <= 3999 字节。
- NVS blob value：1..256 字节。

MQTT（仅 `ESP32BASE_ENABLE_MQTT=1`）：

- Topic：默认 128 字节，构建期允许 32..256。
- payload：默认 512 字节，构建期允许 64..4096；完整入站槽按该值静态分配，提升上限时必须乘以槽数计算静态 RAM。
- 订阅：默认 8，最大 16。
- 完整入站消息槽：默认 2，最大 4。
- 控制事件槽：默认 16，允许 4..16；默认值保证一次连接加 8 个订阅 ACK 不会必然溢出。
- QoS 1 in-flight：默认 4，最大 8。
- ESP-MQTT outbox 预算：默认 4096 字节，允许 1024..16384；Core 2.x 由 wrapper 在 enqueue 前约束，Core 3.x 同时设置底层 outbox limit。
- MQTT task stack：默认 6144 字节，允许 4096..8192；不能把 PlatformIO 静态 RAM 报告当作 task stack 或 TLS heap。
- CA PEM（含末尾 NUL）：默认上限 6144 字节，允许 1024..16384。
- 单个客户端证书/私钥 PEM（含末尾 NUL）：默认上限各 4096 字节，允许 1024..8192；这些数据可能被底层 TLS/MQTT 配置复制，必须纳入初始化 heap 预算。
- 接收消息从 ESP-MQTT 分片复制到固定消息槽一次，业务 callback 直接读取该槽；callback 返回后指针失效。
- `ESP32BASE_MQTT_ALLOW_UNCHECKED_CERTIFICATE_DATES` 只改变配置准入，不增加预握手、任务、缓冲或常驻 RAM；它也不会为官方 Core 补做证书日期校验。

MQTT在IOT Profile中默认开启，在其他Profile中默认关闭。开启后 Arduino Core 预编译 ESP-MQTT 基本同时带入 TLS transport，因此“只配置明文”不能作为显著裁剪 TLS Flash 的手段。发布测量必须分别记录 MQTT 关闭、MQTT 开启但未配置、真实 MQTTS 配置的 `firmware.elf/bin`，并在实机记录初始化、TCP、TLS 握手峰值和稳定连接后的 free/min heap。

Profile重构后的Flash和静态RAM以 `examples/basic` 当前四Profile构建结果为准，不沿用旧Profile的历史数值。`examples/mqtt_tls` 使用不可工作的短CA占位文本，只证明链接和wrapper固定容量，不代表真实CA体积、TLS握手或运行时heap；产品验收不得把构建数值外推为实机资源结论。

人性化容量显示：

- 日志和内置页面中的大字节数只显示 KB/MB/B 人性化值，不重复 raw bytes。
- 状态/API JSON 保留 raw `bytes` 数值，并提供 `human` 字段供前端展示。
- 单位采用二进制换算，`1 KB = 1024 bytes`，`1 MB = 1024 KB`。

Web 响应：

- 优先流式输出。
- 避免大 `String` 拼接。
- 长响应发送过程中让出调度并喂 watchdog。

构建 flags：

- 示例工程默认加入 `-fno-exceptions` 和 `-flto`，并移除 PlatformIO 默认的 `-fno-lto`。Esp32Base 与示例代码不使用 C++ 异常，该 flags 可避免 Arduino / C++ 异常运行时进入固件；LTO 用于让链接器跨 Arduino/Core 库裁掉未使用路径，不改变 profile、API 或功能开关。
- Core 3.x 示例额外通过 `build_unflags` 移除 `-fuse-cxa-atexit`。ESP32 固件不会正常退出进程，Esp32Base 也不依赖进程退出析构注册；如果业务应用存在特殊退出析构语义，应移除此 unflag 并重新评估容量。

Web 页面体积：

- 不再为每页面下发 App Config 专用 CSS（~700 B），只在 App Config 页注入；其他 6 个内置页和业务页首屏均受益。
- `setHeadExtraCallback()` 的业务 head 注入不会作用到 `/esp32base` 内置页面，业务项目的大段应用 CSS 不会增加 Status、System Logs、System 等内置页首屏字节数。
- `/esp32base/status` Status 页不做 LittleFS 全量文件树扫描，只用一次 LittleFS 信息查询读取 used/free/total，并只显示 FileLog 配置摘要；完整 inventory、top files、FileLog 段文件大小和可读性检查保留在 `/esp32base/fs` 或 `/esp32base/logs` 等低频详情页。
- 应用静态资源使用 `ESP32BASE_WEB_MAX_STATIC_ASSETS` 保存 path、content type、数据指针、长度和缓存策略，默认 8 项；不消耗应用 route 表容量。

## 5. PSRAM

当前版本不依赖 PSRAM。

即使板子有 PSRAM，默认路径也不使用 PSRAM 作为必要条件。

未来若使用 PSRAM，必须检查：

- `BOARD_HAS_PSRAM`
- IDF 配置
- 分配失败路径

## 6. 分区建议

建议保留：

- nvs
- otadata
- app0
- app1
- filesystem
- coredump

本库推荐分区表示例优先保证 Arduino / PlatformIO 常规上传流程可靠，默认 balanced 分区表的 `app0` 固定在默认上传地址 `0x10000`，NVS 为 20KB。ESP32 / ESP32-C3 4MB 推荐分区表保留两个 1.5MB OTA app slot，剩余空间作为 LittleFS 数据分区。应用项目应优先从 `partitions/` 目录选择已有分区表；除非硬件容量、OTA 策略或持久化容量确实不匹配，否则不推荐业务项目自定义分区表。

20KB NVS 足够本库默认配置、WiFi 凭证、启动日志环和少量应用配置。推荐分区表不为 NVS 扩容而移动 `app0`；4MB 预设都保持 `app0=0x10000`，避免业务项目额外维护上传偏移。需要更大 NVS 的特殊项目应自定义分区表并同步验证串口烧录、OTA、bootloader、NVS 和分区表一致性。

重启日志环写入量：

- 默认容量 4 条。
- 每次 restart/deep sleep 前写入一条小记录。
- 预期写入频率远低于普通业务配置；若应用存在高频重启，应优先修复重启原因，而不是扩大日志环。

4MB Flash LOCAL/IOT Profile 需要重点验证 OTA app slot 是否足够。

ESP32-S3 8MB 可提供更宽松 app / fs 空间。

ESP32-C3 4MB 要控制 Web/OTA/Fs 组合的体积。

推荐分区表：

| Target | 分区表 | NVS | 单 app slot（最大固件） | LittleFS（最大文件数据） | coredump |
| --- | --- | ---: | ---: | ---: | ---: |
| ESP32 4MB | `partitions/esp32-4mb-ota-balanced.csv` | `20 KB / 0x5000` | `1.50 MB / 0x180000` | `896 KB / 0xE0000` | `64 KB / 0x10000` |
| ESP32-S3 8MB | `partitions/esp32-s3-8mb-ota-balanced.csv` | `20 KB / 0x5000` | `2.25 MB / 0x240000` | `3.38 MB / 0x360000` | `64 KB / 0x10000` |
| ESP32-C3 4MB | `partitions/esp32-c3-4mb-ota-balanced.csv` | `20 KB / 0x5000` | `1.50 MB / 0x180000` | `896 KB / 0xE0000` | `64 KB / 0x10000` |

4MB LOCAL/IOT Profile 必须持续关注 OTA slot 余量。ESP32 / ESP32-C3 4MB 推荐分区表提供 1.5MB 单 app slot；业务若继续增加大页面、证书、图片或大型逻辑，应优先改用 8MB Flash 板型，而不是压缩本库核心逻辑或重新引入多套 4MB 分区预设。

## 7. 容量验收

容量验收以当前构建产物为准，不在长期文档中维护手工复制的 size 表。

发布前至少核对：

- MINIMAL / OFFLINE / LOCAL / IOT的符号存在与裁剪是否符合Profile边界。
- 代表目标的 `firmware.bin` 是否落在推荐分区表的 app slot 内。
- `text / data / bss` 是否出现异常增长。
- 实机 `free heap` / `min free heap` 是否覆盖启动、联网、Web 请求、OTA 和 LittleFS 主要路径。
- 启用 MQTT 时，是否记录 MQTT task stack、TLS 握手峰值、稳定连接 heap、outbox/inbox 高水位和连续重连后的 min heap。

如果某次发布无法完成实机容量验证，应在发布记录中说明缺口，不应把空表或过期数值留在项目文档里。
