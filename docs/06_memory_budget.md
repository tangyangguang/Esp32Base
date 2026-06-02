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

ESP32 full_demo 对照构建中，24 route 相比 16 route 让 `g_routes` 增加 672 bytes BSS。该成本属于可量化的静态 RAM 开销，不应和启动期 NVS 写入、FileLog 初始化写日志或 WiFi 瞬时电流问题混为同一个根因。

### 3.3 Config pending

默认：

```cpp
#define ESP32BASE_EB_CONFIG_PENDING_MAX 8
#define ESP32BASE_CONFIG_PENDING_MAX ESP32BASE_EB_CONFIG_PENDING_MAX
```

每个 pending blob 按实际长度动态分配，单个 blob 最大 256 字节；Config 另有一个 256 字节静态 scratch buffer 用于 blob 写前比较和读取。

### 3.4 FileLog

系统诊断日志仅在 FS profile 中启用，底层实现/API 名称是 `Esp32BaseFileLog`。默认路径为 `/esp32base/logs/system.log`，默认 `4 × 32KB = 128KB`，低优先级缓存 1KB，flush interval 2s。量产推荐 WARN 模式；示例使用 INFO 默认模式方便观察。Core 和默认 NET 不链接 LittleFS，也不产生 FileLog 静态状态。

### 3.5 App Events

应用事件日志默认关闭，仅在 `ESP32BASE_ENABLE_APP_EVENTS=1` 时编译并初始化，依赖 FS。默认容量：

```cpp
#define ESP32BASE_APP_EVENT_LOG_CAPACITY 1024
```

单条记录固定 188 bytes，默认数据区约 188 KiB；加上两个 64-byte header 后文件大小约 188.1 KiB，路径为 `/esp32base/app-events/events.bin`。该容量面向长期运行设备保留约千条可解释业务事件，定位是“近期关键事件窗口”，比 16KB/32KB 小环更适合排查低频业务决策，同时仍明显小于常见 1MB+ LittleFS 分区。

可按项目调整为 `64..2048` 条。若业务事件频率高于“用户可解释的关键事件”，应降低写入频率或把高频明细放到业务自己的数据文件；App Events 不替代调试日志、传感器采样、大 payload、用水量长期统计、报表、累计量、完整执行历史或不允许覆盖的业务数据。

记录字段空间分配：

- `object[56]` 最大，用于业务对象引用。
- `type[24]`、`reason[24]` 用于机器可读分类。
- `source[12]` 用于模块或子系统名。
- `text[32]` 只放短说明，可 UTF-8 截断。
- `value1..value3` 三个 `int32_t` 数值槽，配合 `valueMask` 标记有效性。

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

Web JSON：

- 优先流式输出。
- 避免大 `String` 拼接。
- 状态 API 使用固定 buffer 或 chunked output。

Web 发送 buffer：

- HTML/JSON/CSV 与 PROGMEM CSS/JS 共用 512 B 静态 chunk buffer。响应头仍走 Arduino `WebServer`，正文 data chunk 由基础库写出 `hex\r\n + payload + \r\n` 标准 chunked 帧，避免每个 chunk 触发 `WebServer::sendContent()` 内部小块 `malloc/free`。chunk payload 保持 512 B，不恢复早前实机回归中不稳定的 1 KB/1.4 KB 大 chunk。长响应发送每个 chunk 时会喂 watchdog，避免 UI baseline 增加页面体积后，业务长页面仍在同步 `WebServer::handleClient()` 内就触发 task WDT。若 flush 前后检测到客户端断开，当前响应标记为 broken 并停止继续输出。
- Web 内部已拆为多 `.cpp` 模块，但运行时仍共享同一个 `WebContext` 和 512 B chunk buffer；拆分只改变维护边界，不引入每请求堆分配、页面对象层级或额外响应 buffer。
- 不再为每页面下发 App Config 专用 CSS（~700 B），只在 App Config 页注入；其他 6 个内置页和业务页首屏均受益。
- `setHeadExtraCallback()` 的业务 head 注入不会作用到 `/esp32base` 内置页面，业务项目的大段应用 CSS 不会增加 Status、System Logs、System 等内置页首屏字节数。

## 5. PSRAM

第一版不依赖 PSRAM。

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

本库推荐分区表示例优先保证 Arduino / PlatformIO 常规上传流程可靠，默认 balanced 分区表的 `app0` 固定在默认上传地址 `0x10000`，NVS 为 20KB。应用项目应优先从 `partitions/` 目录选择已有分区表；除非硬件容量、OTA 策略或持久化容量确实不匹配，否则不推荐业务项目自定义分区表。

20KB NVS 足够本库默认配置、WiFi 凭证、启动日志环和少量应用配置。推荐分区表不为 NVS 扩容而移动 `app0`；所有 classic ESP32 4MB 预设都保持 `app0=0x10000`，避免业务项目额外维护上传偏移。需要更大 NVS 的特殊项目应自定义分区表并同步验证串口烧录、OTA、bootloader、NVS 和分区表一致性。

重启日志环写入量：

- 默认容量 4 条。
- 每次 restart/deep sleep 前写入一条小记录。
- 预期写入频率远低于普通业务配置；若应用存在高频重启，应优先修复重启原因，而不是扩大日志环。

4MB Flash FULL profile 需要重点验证 OTA app slot 是否足够。

ESP32-S3 8MB 可提供更宽松 app / fs 空间。

ESP32-C3 4MB 要控制 Web/OTA/Fs 组合的体积。

推荐分区表：

| Target | 分区表 | NVS | 单 app slot（最大固件） | LittleFS（最大文件数据） | coredump | 当前代表 FULL firmware.bin | 余量 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| ESP32 4MB, Core 2.x | `partitions/esp32-4mb-ota-balanced.csv` | `20 KB / 0x5000` | `1.25 MB / 0x140000` | `1.38 MB / 0x160000` | `64 KB / 0x10000` | 1031040 | 280680 |
| ESP32 4MB, large app | `partitions/esp32-4mb-ota-large-app.csv` | `20 KB / 0x5000` | `1.38 MB / 0x160000` | `1.13 MB / 0x120000` | `64 KB / 0x10000` | 1031040 | 410752 |
| ESP32 4MB, large FS | `partitions/esp32-4mb-ota-large-fs.csv` | `20 KB / 0x5000` | `1.00 MB / 0x100000` | `1.88 MB / 0x1E0000` | `64 KB / 0x10000` | 1031040 | -82336 |
| ESP32 4MB, Core 3.x | `partitions/esp32-4mb-ota-balanced.csv` | `20 KB / 0x5000` | `1.25 MB / 0x140000` | `1.38 MB / 0x160000` | `64 KB / 0x10000` | 1177904 | 132816 |
| ESP32-S3 8MB, Core 2.x | `partitions/esp32-s3-8mb-ota-balanced.csv` | `20 KB / 0x5000` | `2.25 MB / 0x240000` | `3.38 MB / 0x360000` | `64 KB / 0x10000` | 862096 | 1497200 |
| ESP32-C3 4MB, Core 2.x | `partitions/esp32-c3-4mb-ota-balanced.csv` | `20 KB / 0x5000` | `1.25 MB / 0x140000` | `1.38 MB / 0x160000` | `64 KB / 0x10000` | 1002064 | 308656 |

4MB FULL profile 的 OTA slot 余量已经明显收窄，ESP32 4MB + Arduino Core 3.x FULL 属于高资源风险组合。首版可以支持，但业务若继续增加大页面、证书、图片或大型逻辑，应优先改用 `partitions/esp32-4mb-ota-large-app.csv` 或 8MB Flash 板型，而不是压缩本库核心逻辑。

## 7. 自动构建资源记录

以下数据来自 `examples/basic` 的 PlatformIO release 构建产物。`firmware.bin` 为实际固件镜像大小；`text` 按 `flash.text + iram0.text` 汇总；`data` / `bss` 来自 DRAM section。ESP32 / Core 2.x 的 7 profile 表应作为同批自动 size 表整体更新，避免同一页混用历史数据。

### 7.1 ESP32 / Arduino Core 2.x

| Profile | firmware.bin | text | data | bss | flash.text | flash.rodata |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| CORE | 285680 | 207954 | 16708 | 9608 | 151131 | 59620 |
| RUNTIME | 336832 | 247658 | 16732 | 11520 | 190159 | 71056 |
| NET | 814608 | 672390 | 25892 | 27592 | 589751 | 108716 |
| NET_RUNTIME | 865600 | 712186 | 25916 | 29504 | 629459 | 119900 |
| WEB | 928480 | 730046 | 26036 | 32648 | 647303 | 164788 |
| WEB_RUNTIME | 1003312 | 783310 | 26060 | 34056 | 700479 | 186332 |
| FULL | 1031040 | 801194 | 26076 | 36528 | 718363 | 196168 |

### 7.2 芯片与 Core 版本代表构建

| Target | firmware.bin | text | data | bss | flash.text | flash.rodata |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| ESP32-S3 CORE, Core 2.x | 277888 | 206730 | 13396 | 5744 | 153155 | 56368 |
| ESP32-S3 FULL, Core 2.x | 862096 | 703966 | 22580 | 28744 | 639619 | 134164 |
| ESP32-C3 CORE, Core 2.x | 272608 | 204964 | 7496 | 6864 | 159682 | 45288 |
| ESP32-C3 FULL, Core 2.x | 1002064 | 802250 | 15964 | 37472 | 747594 | 141712 |
| ESP32 CORE, Core 3.x | 303136 | 206147 | 16977 | 5816 | 144036 | 78572 |
| ESP32 FULL, Core 3.x | 1177904 | 933335 | 26141 | 37731 | 843500 | 216988 |

ESP32-S3 行和 ESP32 Core 3.x CORE 行仍为历史代表构建；冻结前应由同一自动 size 脚本刷新。ESP32 Core 3.x FULL 当前距 1.25MB app slot 仅剩约 132KB，必须纳入 CI size gate。

## 8. 实机资源记录表

以下项目必须通过实机采集补齐，不能由 ELF 静态数据替代。

| 项目 | ESP32 | ESP32-S3 | ESP32-C3 |
| --- | --- | --- | --- |
| boot free heap | | | |
| min free heap | | | |
| WiFi connected free heap | | | |
| Web request min heap | | | |
| OTA min heap | | | |
| LittleFS op min heap | | | |

资源表必须随发布文档维护。发布前至少补齐裁剪、固件体积、boot free heap 和主要场景 min heap；无法自动采集的实机数据必须在发布检查清单中明确记录。
