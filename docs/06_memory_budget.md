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

| Profile | 不应链接 |
| --- | --- |
| CORE | WiFi, WebServer, Update, LittleFS |
| RUNTIME | WiFi, WebServer, Update |
| NET | WebServer, Update |
| NET_RUNTIME | WebServer, Update |
| WEB | Update |
| WEB_RUNTIME | Update |
| FULL | 全部需要的能力 |

关闭模块要求：

- 无对应可选模块 `.inc` 被 `Esp32Base.cpp` 包含。
- 无对应全局对象。
- 无对应静态表。
- 无对应符号进入 map。

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

该上限只控制应用通过 `addRoute()` / `addPage()` / `addApi()` 注册的静态 route 槽位；内置 Web 路由不占用此表。默认 24 是基础库当前 full profile 的设计目标，优先避免业务页面/API 增长时过早触发 route 容量不足。route 较少且需要节省静态 RAM 的应用，可以在构建参数里显式调小。

### 3.3 Config pending

默认：

```cpp
#define ESP32BASE_EB_CONFIG_PENDING_MAX 8
#define ESP32BASE_CONFIG_PENDING_MAX ESP32BASE_EB_CONFIG_PENDING_MAX
```

每个 pending blob 按实际长度动态分配，单个 blob 最大 256 字节；Config 另有一个 256 字节静态 scratch buffer 用于 blob 写前比较和读取。

### 3.4 FileLog

系统诊断日志仅在 FS profile 中启用，底层实现/API 名称是 `Esp32BaseFileLog`。默认路径为 `/esp32base/logs/system.log`，默认 `4 × 32KB = 128KB`，低优先级缓存 1KB，flush interval 2s。默认模式 ERROR，用于让全功能固件在无业务数据、应用事件或显式调试需求时保持很低的 Flash 写入量；现场排查可显式切到 WARN 或 INFO。Core 和默认 NET 不链接 LittleFS，也不产生 FileLog 静态状态。

### 3.5 App Events

App Events默认关闭，仅在 `ESP32BASE_ENABLE_APP_EVENTS=1` 时编译并初始化，依赖RecordStore、FS和Time。默认最大逻辑存储预算：

```cpp
#define ESP32BASE_APP_EVENT_STORE_MAX_BYTES (100UL * 1024UL)
```

App Event业务负载24字节，RecordStore公共元数据和CRC合计24字节，单条固定48字节。100KiB预算的估算容量为2113条；约4KiB完整段轮换时实际保留约2029～2113条，路径为 `/esp32base/records/app-events.v2/`。App Events在业务RecordStore之前创建，不提供单独最低剩余空间配置。

App Events v1同样是每条48字节；v2只重新定义payload末尾4字节，不增加单条LittleFS占用或默认预算。v1目录不迁移、不自动删除，也不参与v2读取；试用阶段升级前应先清空旧事件或由用户明确格式化LittleFS。

项目可以覆盖最大字节预算。若事件频率高于“用户可解释的关键事件”，应降低写入频率或把完整浇水、开关门、喂食历史放到独立RecordStore。

记录字段空间分配：

- `eventCode`、`reasonCode`、`objectId` 各4字节。
- `value1`、`value2` 各4字节。
- `flags`、`level`、`eventKind`、`conditionId` 各1字节。

条件跟踪没有固定槽数组。库内常驻两个32位条件ID位图及少量加载/故障状态；应用每实际使用一个条件，长期持有一个不动态分配的 `ConditionStateTracker`。在当前支持的32位ESP32工具链布局下每个tracker为20字节。NVS只保存 `eb_app_events.active_id_bits` 一个 `uint32_t`；普通观察、确认等待和状态不变均不写NVS或LittleFS，只有确认生效/恢复各追加一条48字节事件并更新一次该整数。按照 [ESP-IDF NVS存储格式](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/storage/nvs_flash.html)，整数值占一个32字节entry；更新会顺序追加新entry并使旧值失效，由NVS按page回收，不是每次转换立即擦除一个Flash sector。显式关闭 `ESP32BASE_ENABLE_APP_EVENT_CONDITIONS` 后不编译条件状态机和专属NVS访问。

通用RecordStore的当前逻辑占用为 `128字节控制文件 + 各段32字节段头 + 记录数 × (payloadSizeBytes + 24)`。业务应在设计固定负载后根据LittleFS分区一次性确定各Store预算；基础库不建立全局预算管理器。段大小由预算自适应选择，详细规划和实机数据见 [Record Store 设计、接入与实机基准](12_record_store.md)。

同时启用 Web 和 RecordStore 时，System 工具页为最多8个当前业务Store保留固定指针登记表，在32位ESP32上为32字节指针空间外加1字节计数；不复制Store状态、payload或记录内容。未启用RecordStore时不编译对应管理页面和处理逻辑。

### 3.6 Health

```cpp
ESP32BASE_HEALTH_TICK_INTERVAL_MS=30000
ESP32BASE_HEALTH_DEBUG_LOG_INTERVAL_MS=1800000
```

### 3.7 Restart log

```cpp
ESP32BASE_RESTART_LOG_CAPACITY=4
```

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

4MB Flash FULL profile 需要重点验证 OTA app slot 是否足够。

ESP32-S3 8MB 可提供更宽松 app / fs 空间。

ESP32-C3 4MB 要控制 Web/OTA/Fs 组合的体积。

推荐分区表：

| Target | 分区表 | NVS | 单 app slot（最大固件） | LittleFS（最大文件数据） | coredump |
| --- | --- | ---: | ---: | ---: | ---: |
| ESP32 4MB | `partitions/esp32-4mb-ota-balanced.csv` | `20 KB / 0x5000` | `1.50 MB / 0x180000` | `896 KB / 0xE0000` | `64 KB / 0x10000` |
| ESP32-S3 8MB | `partitions/esp32-s3-8mb-ota-balanced.csv` | `20 KB / 0x5000` | `2.25 MB / 0x240000` | `3.38 MB / 0x360000` | `64 KB / 0x10000` |
| ESP32-C3 4MB | `partitions/esp32-c3-4mb-ota-balanced.csv` | `20 KB / 0x5000` | `1.50 MB / 0x180000` | `896 KB / 0xE0000` | `64 KB / 0x10000` |

4MB FULL profile 必须持续关注 OTA slot 余量。ESP32 / ESP32-C3 4MB 推荐分区表提供 1.5MB 单 app slot；业务若继续增加大页面、证书、图片或大型逻辑，应优先改用 8MB Flash 板型，而不是压缩本库核心逻辑或重新引入多套 4MB 分区预设。

## 7. 容量验收

容量验收以当前构建产物为准，不在长期文档中维护手工复制的 size 表。

发布前至少核对：

- CORE / NET / WEB / FULL 的符号裁剪是否符合 profile 边界。
- 代表目标的 `firmware.bin` 是否落在推荐分区表的 app slot 内。
- `text / data / bss` 是否出现异常增长。
- 实机 `free heap` / `min free heap` 是否覆盖启动、联网、Web 请求、OTA 和 LittleFS 主要路径。

如果某次发布无法完成实机容量验证，应在发布记录中说明缺口，不应把空表或过期数值留在项目文档里。
