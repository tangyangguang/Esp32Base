# Esp32Base 能力变更记录

本文从 2026-05-06 起记录 Esp32Base 新增和优化的能力，面向正在接入本库的业务项目。业务项目应优先查看本文，了解最近可用的新 API、行为变化和推荐接入方式。

## 2026-05-31

### 应用事件日志

新增：

- 新增默认关闭的 `ESP32BASE_ENABLE_APP_EVENTS` 能力，用于应用项目记录结构化业务/应用事件；该能力依赖 FS，不包含任何具体业务枚举或领域语义。
- 新增 `Esp32BaseAppEventLog` API，支持 `level/source/type/reason/object/code/value1..value3/text` 等通用字段、可信 epoch 或 `bootId+uptimeSec` 时间、固定容量、环形覆盖、分页读取和按需清空。
- 新增 `readStoreInfo()` / `readStoreRecords()` 存储级读取能力，供内置事件日志页面按底层视角展示 slot、offset、record status、stored/calculated `crc16`、`magic/level/crc/committed` 状态和全部 record 字段；业务读取仍使用 `readLatest()` 获取有效事件。
- 默认 `ESP32BASE_APP_EVENT_LOG_CAPACITY=1024`，单条记录 188 bytes，默认存储约 188 KiB；容量可在 `64..2048` 内按项目调整。
- 存储文件为 `/app/events.bin`，使用双 header、记录 `crc16`、header `crc32` 和写后校验。满环覆盖中断恢复时会丢弃未提交槽；单条损坏记录会被跳过且不阻止后续 append。双 header、文件尺寸或写后校验等结构性故障才进入 fault，不自动清空。
- Web 启用时新增独立 App Events 入口：`GET /esp32base/app-events`、`GET /esp32base/api/app-events?offset=0&limit=50`、`GET /esp32base/app-events.csv`、`POST /esp32base/tools/app-events-clear`。页面/API/CSV 支持等级、时间类型、来源、类型、原因和关键词筛选；精确筛选参数复用事件 token 校验，超长或非法时返回 `invalid_filter`，不会截断后误匹配；筛选请求单次扫描完成当前页输出和匹配计数。内置事件日志页面和 CSV 会展示能读出的底层记录，包括 CRC 异常记录并标记状态；JSON API 继续只输出有效事件。时间优先显示可信真实时间，无法解析时显示 `uptime N ms` 和 boot id；JSON/CSV 同时保留 `uptimeSec` 和派生 `uptimeMs`。清空操作位于 System 页面危险操作区，要求 POST、Web Auth 和同源检查。
- 新增 `Esp32BaseWeb::checkPostAllowed(context)`，供业务自定义 POST 复用基础库 Auth 和 Origin/Referer 同源检查。
- 新增 `examples/app_events_demo`，演示启动写入、分页查看、JSON/CSV 输出和手工 POST 写入事件。
- 新增 `scripts/check_app_events.py`，静态检查开关、API、Web 分离、文档和样例。

业务侧影响：

- 业务项目不需要在应用层重复实现 BusinessEventStore；只需在合适业务决策点调用 `Esp32BaseAppEventLog::append()` 并定义自己的事件命名。
- App Events 是面向长期运行设备的“近期关键事件窗口”，不是业务长期数据模型。用水量长期统计、报表、累计量、传感器采样历史、完整执行历史、大 payload、高频明细和不允许覆盖的业务数据，应继续由业务自己的数据模型负责。
- App Events 也不是第二套系统 FileLog。业务项目不要把 boot/reset/restart reason、WiFi、NTP、OTA、LittleFS mount/write fault、FileLog fault、基础库健康状态等 Esp32Base 系统事件写入 App Events；同一故障可同时有 FileLog 和 App Event，但 FileLog 写技术原因和内部错误链路，App Event 写业务含义和用户可理解结果。
- 业务项目需要面向业务用户展示事件时，应使用 `readLatest()` 或 `/esp32base/api/app-events` 创建自己的业务事件列表和详情页，用业务语言解释 `source/type/reason/code/value`，不展示 `magic/crc16/reserved/valueMask/flags` 等内部字段。
- `/esp32base/logs` 仍只展示 Esp32Base/FileLog 系统日志；应用事件单独进入 `/esp32base/app-events`，避免和 WiFi、OTA、NTP、启动、健康状态等基础库诊断混在一起。
- 恢复出厂或清空业务记录时，业务可按需调用 `Esp32BaseAppEventLog::clear()`；基础库的普通系统配置清理不会隐式删除应用事件。

### Web UI 原生 dialog 基线

新增：

- 基础 `/esp32base/ui.css` 现在覆盖原生 `dialog`、`dialog::backdrop`、`dialog.panel` 和推荐的 `dialog.eb-modal`，用于业务页确认弹层和 1-3 个字段的小表单。
- 弹层复用现有低饱和变量和按钮层级：白色内容面、浅边框、轻阴影、克制遮罩、8px 圆角和紧凑内边距，避免浏览器默认黑边框和厚重按钮。
- 原生 dialog 内的 `fieldgrid`、`field`、`actions`、`btnlink`、`secondary` 会自然对齐；手机宽度下弹层自动收敛到近全宽并限制最大高度。
- `examples/web_ui_gallery` 增加原生 `<dialog class="panel eb-modal">` 小表单示例，静态检查覆盖 dialog baseline 选择器。

业务侧影响：

- Esp32_Irrigation 这类业务库可把手动浇水确认写成 `<dialog class="panel eb-modal">`，继续复用基础库 `fieldgrid`、`actions` 和按钮类，不需要维护整套业务 modal CSS。
- 基础库仍保留 `sendInfoRowDialogForm()` 自定义弹层 helper；原生 `<dialog>` 样式只是补齐 baseline，不改变现有内置页面路由、认证和导航行为。

### 根路径业务首页

新增：

- `HOME_APP` 和 `HOME_COMBINED` 下，业务模式默认优先把已注册的 `/index` 页面作为业务首页；未显式调用 `setHomePath()` 时，`/` 会跳转到 `/index`。
- 应用显式 `setHomePath("/")` 并注册根路径 GET 业务页面时，`/` 直接调用业务 handler，不再对 `configuredHomePath()` 产生自跳转。
- `/esp32base` 保持系统入口行为：`HOME_COMBINED` 下继续展示融合系统首页，业务根首页不会覆盖系统直达入口。

业务侧影响：

- 新业务建议把业务首页注册为 `/index`，让裸 `/` 只做稳定跳转；确实需要裸根路径渲染时，再显式使用 `setHomePath("/")`。

### Web UI 局部交互与小表单弹层

新增：

- 新增 `sendInfoRowInlineEdit()`，用于单字段行内编辑；支持当前行展开、AJAX 局部保存、成功后替换目标片段，禁用 JavaScript 时保留普通 POST fallback。
- 新增 `sendInfoRowDialogForm()`，用于 1-3 个字段的小表单弹层；桌面端居中小弹窗，手机端贴底/近全宽，保存成功后关闭弹层并局部更新目标行。
- 新增 `isAjaxRequest()`、`sendAjaxReplace()` 和 `sendAjaxError()`，统一 `X-Esp32Base-Ajax: 1` 请求的 JSON 成功/失败返回。
- 基础 `WEB_HEAD` 增加轻量运行时，只接管带 `data-eb-ajax`、`data-eb-inline-toggle` 和 `data-eb-dialog-open` 标记的元素，不影响普通表单。
- `examples/web_ui_gallery` 增加行内编辑和小表单弹层示例，并覆盖 JSON 成功、JSON 错误和普通 fallback 行为。

业务侧影响：

- 简单配置项可以不再跳转新页面或整页刷新，页面滚动位置不会因为保存而丢失。
- 重启、格式化、清日志、OTA、文件上传/下载/删除和 Auth 修改不默认局部刷新，仍建议保留明确确认、进度或整页结果。

### WiFi 初始化期安全启动保护

新增：

- `Esp32BaseWiFi::begin()` 现在会在任何 Arduino `WiFi.*` 初始化/模式调用前写入 `eb_wifi.init_guard`。如果设备连续在 WiFi 初始化最早期以 brownout、panic、watchdog 或 `software` reset 重启，达到 `ESP32BASE_WIFI_SAFE_BOOT_MAX_RESETS` 后会设置 `eb_wifi.init_pause=true`，本轮跳过所有 WiFi 初始化调用，让系统至少进入无 WiFi 的安全运行/串口诊断状态。
- 触发暂停时新增醒目中文日志：提示“疑似 WiFi/RF 启动瞬时电流导致供电跌落”，建议检查电源、USB 线、稳压器余量和接线，并考虑在板端 VIN/5V 与 GND 间增加低 ESR 储能电容；日志同时明确不会关闭 brownout detector。
- WiFi 初始化 guard 与既有 STA guard 分离：前者覆盖 `WiFi.persistent()`、`WiFi.setSleep()`、`WiFi.mode()`、`WiFi.begin()` 和 AP 初始化之前的最早期启动风险；后者继续覆盖已保存 STA 凭据进入连接阶段后的异常复位。
- 新增检查项到 `scripts/check_wifi_safe_boot.py`，覆盖 init guard NVS key、`ESP_RST_SW` 分类、中文供电提示和诊断文档。

业务侧影响：

- 对 Esp32_Irrigation 这类弱供电实机，连续停在 `module_begin name=wifi` 后 brownout/SW reset 的场景不再永久重启；达到阈值后会跳过 WiFi，让应用至少完成基础启动并通过串口给出明确供电诊断建议。
- 该机制不把电容不足作为唯一原因，不关闭 brownout detector，也不替代硬件排查；业务侧仍应检查电源限流、USB 线压降、稳压器余量、接线接触电阻，并按需要补充板端储能电容。

### Web UI 按钮视觉层级

优化：

- 基础 `/esp32base/ui.css` 增加 `--eb-primary-hover`、`--eb-button-soft`、`--eb-button-soft-hover` 和 `--eb-button-border` 变量，业务换肤时可同时覆盖主按钮 hover 和普通浅按钮，不再残留固定 hover 色。
- 默认按钮高度从重色 32px 收敛到更轻的 30px；普通入口、紧凑行动作和分页按钮使用浅底描边，明确保存/执行类动作仍保留主按钮。
- 修复 System 页面低频入口的 Open 按钮继承通用行内动作最小宽度后跨出 72px 动作列的问题，入口按钮现在保持紧凑右对齐，不再遮挡左右两列内容。
- 危险动作按钮改为浅危险色和红色边框，继续配合 `dangerpanel`、确认文案和服务端 POST 校验表达风险，避免维护页出现大面积强警告色。
- `sendInfoRowCompactForm()` 现在和 `sendInfoRowCompactLink()` 一样输出 `.btnlink` tone class，`UI_INFO`、`UI_OK`、`UI_WARN`、`UI_DANGER` 和 `UI_NEUTRAL` 的行内 POST 按钮视觉层级保持一致。
- 修复 OTA diagnostics 面板使用 `<div>` 包裹 `<tr>` 的非法 HTML，改为 `tablewrap + kv` 表格结构。
- 新增 `scripts/check_web_ui_baseline.py` 静态检查，覆盖按钮变量、工具入口按钮列宽、form/link tone 一致性、OTA diagnostics 表格结构和文档变更标记。

业务侧影响：

- 业务页面继续使用 `sendHeader()`、helper 和基础 CSS 即可获得更克制的按钮外观；如果已有自定义主题，只需按需补充 `--eb-primary-hover`、`--eb-button-soft`、`--eb-button-soft-hover` 和 `--eb-button-border`。
- 已经依赖 `sendInfoRowCompactForm()` 的页面会从主色 submit 按钮切换为轻量 tone 按钮；这符合低频行内动作的推荐语义。

### 双 OTA 串口恢复脚本可靠性优化

修复：

- OTA boot 诊断和 rollback/mark-valid 状态现在在 `Esp32Base::begin()` 早期初始化，不再等待 Web ready；ArduinoOTA/espota 网络服务仍等 Web/Auth ready 后启动，避免新固件 WiFi/Web 未就绪时跳过 mark-valid timeout。
- OTA 启动日志新增 INFO 级 `boot_summary`，一行输出当前运行槽、configured boot 槽、next update 槽、地址和 ELF SHA256；Web OTA/ArduinoOTA 上传日志新增 `stage=begin/write/verify/set_boot/boot_confirmed/done`，失败日志带当前阶段、目标槽和进度，便于定位是写入、校验还是启动槽切换问题。
- `scripts/esp32base_serial_recover_ota.py` 不再在写完 bootloader、partition table、boot_app0 和两个 OTA app 槽后另起一条 `erase_region` 清理 `otadata`。
- 脚本现在生成与 `data/ota` 分区等长的全 `0xFF` 临时镜像，并把它随其他镜像一起放进同一次 `write_flash`，避免第二次 esptool 连接失败导致“两个 app 槽已 Hash verified，但 `otadata` 未清理”的恢复假象。
- 当 `boot_app0` 默认偏移与 `otadata` 分区重叠时，脚本会跳过 `boot_app0`，以本次恢复要求的 `otadata` 清理为准，避免同一条 `write_flash` 命令重复写同一地址。
- 恢复脚本的分区解析支持 `K`/`M` size 和空 offset 自动计算，覆盖 Arduino/ESP-IDF 常见自定义分区表写法。
- dry-run 输出会明确列出检测到的 OTA 槽、实际写入槽、`otadata` 清理方式和完整 flash plan；执行失败时提示 bootloader 可能仍按旧 `otadata` 选择先前槽位。
- 脚本执行前会在 macOS/Linux 上尽量检测串口端口占用，避免 `pio device monitor` 未关闭时进入不稳定烧录；同时刷新 flash plan 输出，保证 esptool 日志不会排在恢复计划前面。
- 新增 `scripts/check_ota_boot_safety.py` 和扩展 `scripts/check_serial_recover_ota.py`，覆盖 OTA early rollback 初始化、ESP32_Faucet 这类 `otadata=0x19000`、`ota_0=0x20000`、`ota_1=0x180000` 分区表、`boot_app0` 与 `otadata` 同为 `0xe000` 的常见分区表，以及 `K`/`M` 和空 offset 分区表。

业务侧影响：

- WebOTA 后再做串口恢复时，推荐继续使用恢复脚本；普通串口 upload 只写固定 app 槽，不一定覆盖当前启动槽，也不会自动改写 `otadata`。
- 手写 esptool 恢复命令时，应优先在同一次 `write_flash` 中同时写两个 OTA 槽和全 `0xFF` 的 `otadata` 镜像；把 `erase_region` 放到第二条命令会重新依赖板子的下载模式进入时序。

### FS 定长文件初始化 API

新增：

- 新增 `Esp32BaseFs::createFixedFile()`，签名为 `createFixedFile(const char* path, uint32_t size, uint8_t fillByte = 0)`，用于 RecordStore/EventStore 这类二进制定长环形文件初始化。
- `createFixedFile()` 要求 path 为绝对路径，`size == 0` 会创建或截断为空文件；文件不存在、大小不匹配，或同尺寸但末端不可读时会分块重建并填充 `fillByte`。
- 已存在且大小一致、末端可读的文件会直接返回 true 并保留内容，避免业务项目每次启动清空持久环形记录。
- 重建过程使用固定小块写入，不一次性占用大 heap，并按长 FS 操作处理 task watchdog；完成后校验最终文件大小，并抽样校验首字节、中间字节和末尾字节。
- `full_demo` selftest 增加 16KB、32KB、64KB 定长文件创建和首/中/尾读取验证。

业务侧影响：

- 需要固定容量二进制环形文件时，业务项目应改用 `createFixedFile()` 初始化或校验容量，再用 `readBytesAt()` / `writeBytesAt()` 做定点读写。
- `appendBytes()` 仍适合低频追加；它每次都会打开、flush、校验大小和末端可读，不建议作为大文件 bulk 初始化循环。

### WiFi STA 安全启动恢复体验优化

优化：

- `ESP32BASE_WIFI_SAFE_BOOT_MAX_RESETS` 默认值从 2 调整为 3，降低偶发 guarded brownout/panic/watchdog 复位导致 WiFi 凭据暂停的概率。
- 已进入 `sta_pause=true` 后，如果后续启动 reset reason 是 `poweron`、外部复位或其他非危险复位，库会自动清除 pause/guard/count 并用已保存凭据恢复一次 STA 尝试，不再要求用户把正确密码重新保存一遍。
- 新增 `Esp32BaseWiFi::retrySavedCredentials()`、`safeBootPaused()` 和 `safeBootGuardedResetCount()`；WiFi 页面在 safe boot paused 时显示恢复说明和 guarded reset 计数，并提供“Retry Saved WiFi”按钮。
- 新增 `/esp32base/api/wifi/retry` 表单端点，用于恢复并重试已保存 WiFi 凭据，不修改 `ssid/pass`。

业务侧影响：

- 已保存 WiFi 密码正确但曾因安全启动保护进入 AP 配网页的设备，正常重新上电后会自动尝试恢复；如果仍停留在 AP 页面，可点击 Retry Saved WiFi，不需要重新输入或保存同一组密码。
- 如果一恢复 STA 就再次发生 brownout、panic 或 watchdog 复位，保护仍会重新累计并回退 AP，业务侧仍应排查供电余量、射频启动路径和启动期 watchdog。

### Web 内部多编译单元重构

优化：

- Web 实现从单个巨型 `Esp32BaseWeb.inc` 拆为 `Esp32BaseWeb.cpp` facade、`WebContext`、`WebResponse`、`WebLayout`、`WebAssets` 和按功能分组的 Status/WiFi/Auth/Tools/Logs/FS/OTA/AppConfig 模块。
- `library.json` 现在显式编译 `src/web/*.cpp` 与 `src/web/internal/*.cpp`；非 Web profile 仍由 profile 宏裁剪，不应链接 WebServer、Update、LittleFS 等重依赖。
- 发送路径继续使用同一个 512 B chunk buffer、PROGMEM 资源和 raw chunked writer，不新增页面对象层级、动态分配或大 `String` 拼接。
- FS 管理页上传表单将目录选择、文件选择和精简说明文案分离到稳定栅格，避免桌面宽屏下辅助说明被压窄换行。
- Web/FS/OTA/FileLog 相关检查脚本改为检查新的 Web 模块集合，不再依赖旧 `.inc` 文件。

业务侧影响：

- 公开 API、HTTP 路由、页面行为、CSS 缓存、Auth、OTA、FS、Logs 和 App Config 语义不变；业务项目正常 include `Esp32Base.h` 即可。
- 自定义 PlatformIO 发布或打包流程如果覆盖了本库 `library.json` 的 `srcFilter`，需要确认包含 `src/web/*.cpp` 和 `src/web/internal/*.cpp`。

## 2026-05-30

### FS 上传目录选择与写入校验收紧

修复：

- `/esp32base/fs?manage=1` 的上传目录下拉框现在会列出任何已有目录，包括 `/logs` 等 FileLog 目录，便于维护和实验场景导入旧日志文件。
- 上传目标路径拼接如果超过内部路径上限会直接拒绝，不再接受被截断后的路径。
- 上传到 FileLog 路径前会先 flush，上传结束后重新加载 FileLog 运行态。
- 上传完成后会校验最终文件大小和末端可读性，避免只看文件存在就返回成功。
- `scripts/check_fs_upload.py` 补充任意已有目录上传、路径截断拒绝和完成校验检查。

业务侧影响：

- 管理模式面向维护/实验人员，不做普通用户级目录屏蔽。上传到业务运行中会读取的路径后，业务侧仍需自行处理格式校验、索引重建或重启提示。

### 双 OTA 串口恢复与 OTA 槽位诊断

修复：

- Web OTA 完成 `Update.end(true)` 后会读取 configured boot partition，确认下一启动槽已经切到本次写入目标；如果 boot partition 与目标槽不一致，OTA 返回失败并保留错误日志，不再给出含糊成功状态。
- Web OTA 和 ArduinoOTA 上传开始日志会输出写入目标 OTA 槽与地址；上传成功日志会输出已设置的 boot partition 与地址。
- OTA 启动诊断会输出当前 running partition、configured boot partition 和 running ELF SHA256，便于串口日志判断当前实际运行的是哪个固件。
- `/esp32base/api/ota` 增加 `expectedSha256`、`calculatedSha256`、`lastTargetPartition`、`lastBootPartition`、`runningPartition`、`bootPartition`、`nextUpdatePartition` 和 `appPartitions`，每个 app 分区包含地址、大小、OTA state、镜像 SHA256 和版本信息。
- Web OTA 对 `X-Firmware-Size` 做严格数字解析，非数字、空值或溢出值会在开始写 flash 前拒绝。

新增：

- 新增 `scripts/esp32base_serial_recover_ota.py`，可按 PlatformIO env 的 partition table 自动定位 `otadata`、`ota_0`、`ota_1`，默认写 bootloader、partition table、boot_app0，把当前 `firmware.bin` 写入所有 OTA app 槽，并擦除 `otadata`。
- 新增 `scripts/check_ota_recovery_diagnostics.py`，检查 OTA 槽位诊断、SHA 暴露、恢复脚本和文档边界。

业务侧影响：

- 双 OTA 设备通过 Web OTA 切到 `ota_1` 后，普通串口上传只写 `ota_0` 不会自动改变 `otadata`，设备可能继续从旧槽启动。恢复时应写当前启动槽或两个 OTA 槽，并清除 `otadata`；推荐直接使用 `esp32base_serial_recover_ota.py --dry-run` 确认命令后执行。

### WiFi STA 安全启动保护

新增：

- 有已保存 WiFi 凭据时，STA 启动前会写入 `eb_wifi.sta_guard` guarded 标记；成功连接后清除 guard、计数和 pause。
- 如果设备在 guarded STA 启动后连续以 brownout、panic 或 watchdog 类 reset reason 重启，达到 `ESP32BASE_WIFI_SAFE_BOOT_MAX_RESETS` 后会设置 `eb_wifi.sta_pause=true`，保留原 `ssid/pass`，暂停 STA 并自动进入 AP config portal。
- 重新提交 WiFi 凭据、点击 Retry Saved WiFi 或后续非危险复位自动恢复，都会清除 pause/guard/count 并恢复 STA 尝试；普通连接超时仍按原有 backoff 重试，不因路由器临时离线直接开放 AP。
- 新增 `scripts/check_wifi_safe_boot.py`，检查 STA 安全启动保护代码标记、日志标记和文档边界。

业务侧影响：

- ESP32_Faucet 这类设备如果因保存的 STA 状态在 `WiFi.mode(WIFI_STA)` 附近触发 brownout 重启，不会永久卡在重启循环；连续异常后可通过 `ESP32-Config-XXXX` AP 重新配网。
- 该机制不关闭 brownout detector，也不掩盖供电问题；日志中的 `sta_safe_boot_pause` 表示需要同时检查供电余量和当前 WiFi 凭据/射频启动路径。

### FS 管理模式支持受限上传

新增：

- `/esp32base/fs?manage=1` 增加单文件上传，用于测试环境导入真实场景下载下来的业务数据文件。
- 上传保留本地文件名，只能选择已有 LittleFS 目录，不创建目录；目标文件存在时，前端先调用 `/esp32base/fs/check` 检查并弹确认框，确认后才带 `overwrite=1` 上传。
- 新增 `GET /esp32base/fs/check?dir=/data&name=file.bin` 和 `POST /esp32base/fs/upload`，复用 Web Auth、同源 POST 和服务器端路径校验。
- 新增 `scripts/check_fs_upload.py`，检查上传路由、冲突确认、目录选择和文档边界说明。

业务侧影响：

- 上传只负责把文件写入 LittleFS，不校验业务数据格式、索引、NVS 状态或运行时缓存。业务如果希望导入后完全等价于自己生成的数据，应自行在应用层提供格式校验、索引重建或重启提示。

### 仓库历史/过程文件清理

文档：

- 删除 `bug-ticklish-kitten.md` 以及 `design-history/` 下已过期的历史方案、评审和评估文件；这些内容不再作为仓库内交付资料。
- README、设计说明和发布检查清单同步移除“查看 `design-history/` 归档”的表述；后续以 `README.md`、`docs/`、`CHANGELOG.md` 和现有代码为准。

## 2026-05-29

### FS API 读写失败语义收紧

修复：

- `Esp32BaseFs::readBytes()` / `readBytesAt()` 不再只要文件能打开就返回成功；当 LittleFS 元数据声明还有内容、但底层实际读出 0 字节时返回 `false`，避免业务把损坏或不可读文件误判为正常空读。
- `writeFile()` / `writeBytes()` / `appendFile()` / `appendBytes()` 写入后会 flush，并校验最终文件大小；非空写入还会确认写入后文件末端可读。
- `writeBytesAt()` 覆盖写入后会校验文件大小未变，并确认覆盖范围末端可读；如果 FS 满、底层写失败或已有文件损坏，返回 `false`。
- FileLog 的普通追加、清空和轮转截断都会按长 FS 操作处理；当 FS 已满或损坏时，系统级 WARN 日志写入失败不应再触发 task WDT 重启。检测到 FileLog 写入故障后，本轮会先在运行时停止继续写 FileLog，避免每条后续 WARN 都重复冲击异常文件系统；Web 清理或格式化后可再重新启用模式。
- `Esp32BaseFileLog::faulted()` 暴露运行期故障保护状态；Status、Logs 和 System 页会把“配置开启但运行保护停写”显示为 `write fault`，并说明已有日志可能仍可读取，不再误显示为 `disabled`。
- Logs 和 System 页现在把 `write fault` 说明显示为 WARN notice；FileLog 关闭时显示 INFO notice，明确表示新日志不会写入但旧日志仍可查看。
- `/esp32base/fs?manage=1` 对不可读文件仍提供单文件删除入口，但继续隐藏下载入口，便于清理坏文件且避免浏览器保存 0 字节伪成功文件。
- 单文件删除在 `LittleFS.remove()` 失败后会尝试把文件截断为 0；如果成功释放可见文件占用，页面显示 `File deleted or cleared`。删除或清理文件后，如果 FileLog 之前处于写入故障保护，会重新加载当前 FileLog 模式尝试恢复写入。
- 新增 `scripts/check_fs_api_reliability.py`，防止后续把 FS 读写失败语义退回到“只看 open/write 返回值”的弱判断。

业务侧影响：

- 业务项目必须检查所有 `Esp32BaseFs` 写入和读取返回值；连续失败时应停止继续追加记录，进入清理、提示维护或格式化前确认流程。
- LittleFS 的逻辑文件大小不等于内容一定可读。跨项目反复烧录、brownout、FS 满或旧业务代码忽略写失败后，文件树可能仍能列出大小，但读取内容会失败；此时 `/esp32base/fs` 会标记 `unreadable`。
- 如果 Web 显示 FileLog 为 `write fault`，表示配置模式仍是 ERROR/WARN/INFO，但底层 FS 写入失败触发运行期保护；日志页面能显示旧内容并不代表新日志仍能写入。先通过 `/esp32base/fs` 清理坏文件或在 System 页格式化 LittleFS，再重新保存 FileLog 模式。
- 如果删除 FileLog 文件仍失败，通常说明 LittleFS 元数据或内部占用已经异常；优先用 System 页 `Clear logs` 清空日志段，仍不能恢复时再确认格式化 LittleFS。

### 历史安全边界文档同步

文档：

- `docs/10_known_limitations.md` 补齐默认 `admin/admin` 且不强制首次改密、开放 config portal AP、普通 NVS 明文凭据、危险 POST 的轻量同源检查边界，以及诊断端点信息量大且不做 rate limit 的说明。
- `docs/03_api.md` 补充 Config deferred 的 `millis()` 回绕边界、Web Auth 超长 Authorization header 拒绝行为，以及 config portal AP 默认开放的 API 语义。
- 修正 OTA 上传页和发布检查清单中的容量展示说明：内置页面只显示 KB/MB/B 人性化值，状态/API JSON 保留 raw `bytes`。

## 2026-05-28

### System 页工具布局收敛

优化：

- `/esp32base/tools` 的低频入口从横向 quicklinks 改为紧凑信息行，入口用途更清楚，不再依赖一串链接表达。
- Hostname、Footer bar、Watchdog trip 和 File log 进入两列 `toolgrid`，PC 上减少纵向滚动；手机端仍保持单列。
- Restart、Format LittleFS 和 Clear logs 仍使用危险操作面板，但也进入维护网格，和普通设置区分开。
- System settings 入口进一步改为 PC 两列按钮网格，App Config、WiFi Setup、Web Auth 和 Firmware OTA 直接点击进入，不再显示额外 Open 列。
- Hostname 面板压缩为紧凑表格和同一行编辑区，减少单个面板在 PC 上撑高整行。
- WiFi 和 System 页的操作结果提示统一挪到页面标题下方，先反馈保存/失败结果，再显示具体设置块。
- System settings 入口在手机端保持“说明 + Open”左右结构，减少入口区纵向占用，同时保留说明文字。

业务侧影响：

- System 页 URL、POST 参数、确认弹窗、PRG 流程、NVS key 和维护动作语义不变。
- 业务工具页如果是“低频入口 + 若干设置 + 危险维护动作”，应优先复用紧凑行、`toolgrid`、`formpanel`、`actionpanel` 和 `dangerpanel`。

### App Config 保存前核对区配色优化

优化：

- `/esp32base/app-config` 的 Confirm changes 区域改为中性 review 样式，移除大面积成功色背景，避免用户误以为变更已经保存。
- Cancel 按钮改为低权重中性按钮，只保留 Confirm Save 作为主操作；分组行同步改为中性灰底。
- 未注册 App Config group 或字段时显示轻量空状态，而不是一行弱提示。

业务侧影响：

- App Config 保存流程、revision 防旧页面提交、POST 参数、校验语义和 pending restart 行为不变。
- 业务页面的保存前核对区也应使用中性 review 语义，成功色只用于保存完成后的结果反馈。

### Logs 页恢复紧凑诊断表

优化：

- `/esp32base/logs` 恢复为紧凑元信息表格，不再使用指标卡片展示 FileLog 状态，避免诊断页显得复杂和分散。
- FileLog 的 enabled、path、rotation files、mode、buffer、flush interval、max per file、max total 和 segments 继续集中显示。
- segment 标签、Open raw log 链接和 raw iframe 保持原有行为，日志正文仍通过 `/esp32base/logs/raw?segment=N` 加载，不内联进主页面。
- Clear logs 完成后回到 Logs 页并显示成功或失败提示；刷新页面不会重复提交。
- FS/FileLog 不可用时改为轻量不可用面板，避免只有一行弱提示。

业务侧影响：

- Logs 页 URL、Basic Auth、segment 查询参数、raw endpoint、System 页 Clear logs 入口和日志流式输出语义不变。
- 业务诊断页不是所有场景都需要卡片；日志、原始片段、系统元信息这类高密度内容优先使用紧凑表格。

### 字节数人性化展示统一

优化：

- `Esp32BaseLog::formatBytes()` 改为只输出 `B`、`KB`、`MB` 人性化值，不再重复 raw bytes。
- 启动诊断、资源日志、FS mount、FileLog 模式审计、OTA 分区调试和 Config/Web 长度诊断中的字节数统一使用人性化值。
- Web OTA 上传页进度只显示友好容量；状态/API JSON 继续保留 raw `bytes` 数值，并通过 `human` 字段提供展示文本。

业务侧影响：

- 串口日志、FileLog 和内置页面更易读；需要精确数值的自动化脚本应优先读取状态/API JSON 的 raw `bytes` 字段。

### Web 长响应 watchdog 修复

修复：

- 新增 `Esp32BaseWeb::sendResponseHeader(name, value)`，允许业务 handler 在 `beginResponse()`、`sendJson()`、`sendText()` 等发送响应前追加安全校验过的 HTTP 响应头；可用于应用 CSS/JS/静态资源设置 `Cache-Control`，避免把资源外置后仍无法明确浏览器缓存。
- `sendResponseContent()` 在每个 chunk 发送前后主动喂 watchdog，避免大 HTML/JSON/CSV 响应仍处在 `WebServer::handleClient()` 内时超过 task WDT 窗口。
- 长响应数据 chunk 改为基础库无堆分配 writer 输出，避免每个 512 B chunk 都触发 Arduino `WebServer::sendContent()` 内部的小块 `malloc/free`；响应头、chunked 协议格式和最终收尾语义保持不变。
- `setHeadExtraCallback()` 不再注入到 `/esp32base` 及其子路径的内置页面，避免业务项目的大段页面 CSS 跟随 Status、Logs、System 等内置页重复下发；业务页面和自定义路由继续保留该 head 注入能力。
- 修复 UI baseline 增加页面基础体积后，业务长页面或慢速客户端可能在响应未结束前触发 watchdog reset 的问题。
- 慢请求日志、chunk buffer 大小、URL、route/API 和业务页面 helper 语义不变。

业务侧影响：

- 业务应用不需要为了这个 watchdog 问题绕开基础库或拆改现有页面；仍建议长列表页面保留分页，避免同步 WebServer 长时间占用主循环。
- 内置 `/esp32base` 页面不再承载业务 CSS 字节，已经通过 `setHeadExtraCallback()` 注入样式的业务页面无需改动。
- 业务项目把 CSS/JS 从内联改为独立资源时，推荐在资源 handler 中先调用 `sendResponseHeader("Cache-Control", "public, max-age=86400")`，再调用 `beginResponse()` 输出资源；资源 URL 应带版本号或固件版本查询参数，避免升级后浏览器继续使用旧缓存。
- 实机横向测试显示内置页、内置 API、CSS、业务首页/统计/预设/滤芯页和业务 API 均可正常返回；如果某个业务 HTML 页仍显著慢于同数据 API，应继续在业务 handler 的额外文件查询或逐行渲染路径排查。

### FileLog 启动重复日志修复

修复：

- `Esp32BaseFileLog::begin()` 只加载已保存模式，不再把 NVS 中的模式误记为运行期 `mode_changed`。
- `mode_changed` / `mode_unchanged` 只保留给显式 `setMode()` 操作；相同模式使用 DEBUG，真实变更仍使用 WARN，便于追踪系统级配置修改。
- 避免 brownout 或重启循环时 4 个 history segment 被同一条 FileLog 初始化日志快速刷满。

业务侧影响：

- Logs 页面仍保留 current/history segment 结构；已写入的旧重复日志不会自动删除，可在 System 页面执行 Clear logs 清理。

### 启动期 NTP boot session 写入修复

修复：

- `Esp32BaseNtp::initBootSession()` 不再每次启动同步写 `eb_sys.time_boot_id`，改为复用 `Esp32BaseSystem::bootCount()` 作为当前 boot 的 `bootId`。
- 避免启动早期连续 NVS 写入在弱供电或重启循环中放大 brownout 风险，同时减少不必要的 flash/NVS 写入。
- `time_boot_session` 日志增加 `source=boot_count`，便于确认 bootId 来源。

业务侧影响：

- `snapshot().bootId`、`isCurrentBootEvent()`、`resolveCurrentBootEvent()` 的业务语义不变，仍只允许回填本次 boot 的相对时间事件。
- 旧的 `eb_sys.time_boot_id` key 不再更新；业务项目不应直接依赖该 NVS key。

### Web 应用路由默认容量统一为 24

设计：

- Web 应用路由默认容量统一为 24，作为基础库 full profile 当前目标默认值，优先覆盖业务页面/API 增长时的容量需求。
- 该上限只影响业务通过 `addRoute()`、`addPage()` 和 `addApi()` 注册的静态 route 槽位；内置 Web 路由不占用此表。
- ESP32 full_demo 对照构建显示，24 route 相比 16 route 让 `g_routes` 增加 672 bytes BSS；这是可量化的静态 RAM 成本，不作为 brownout 原始根因处理。

业务侧影响：

- route 较少且需要节省静态 RAM 的业务项目，可在核算 route 数量后通过构建参数显式调小 `ESP32BASE_WEB_MAX_ROUTES`。
- 页面/API 继续增长时，业务项目仍可按项目显式调大 `ESP32BASE_WEB_MAX_ROUTES`，并同步验证 heap/static RAM 边界。

### Status 页视觉深化

优化：

- `/esp32base` Status 页调整为诊断优先结构，不再显示和相邻详细区重复的 `System Overview` 预览块。
- 常用信息直接前置为 Device、Network 两个正式分区，后续按 Runtime Health、Storage & Logs、Firmware & OTA、Hardware 展开，Partition Table 保留为最后的详细诊断表；reset/wake 原因归入 Runtime Health，不再单独占用 Boot Reasons 分区。
- Network 的 STA MAC、AP MAC 改回独立键值行；Hardware 的 eFuse MAC 也使用普通键值行，避免把二级标签塞进值列导致对应关系不清。
- Heap、Watchdog、FS 和 FileLog 使用轻量子指标展示，避免把多个诊断值塞进同一句话导致换行和阅读困难。
- Storage & Logs 新增 File inventory 摘要和 File details 入口，显示文件数、目录数、已统计文件大小、other/overhead；File details 入口并入 FS 行，FileLog level/current/used/limit 合并为一行子指标，不再单独占多行，并把 Top 文件列表收敛到详情页，避免状态页被文件列表撑高。
- 新增 `/esp32base/fs` 详情页，默认只读，显示对齐后的 Summary、Top 10 最大文件和最多 128 项文件树；文件树提供单文件下载；当文件声明有大小但首块无法读取时标记 `unreadable`，下载路由返回 `500 File read failed`，避免浏览器保存 0 字节伪成功文件；当底层 `FS used` 明显大于可见文件合计时显示内部/历史占用告警。
- `/esp32base/fs?manage=1` 提供显式管理模式，只支持单文件删除；删除走 `POST /esp32base/fs/delete -> 303 -> GET`，复用认证和同源保护，不提供目录删除、批量删除、编辑或格式化入口。
- FileLog 归入 Storage & Logs，并把启用状态、日志级别、当前文件大小、日志总占用/上限压缩成紧凑子指标，路径保留独立行，避免把 WARN 日志级别误读成设备健康告警。
- Firmware & OTA 将长标签 `OTA slot minus current sketch` 调整为 `OTA headroom`，并在值下方保留 `target slot - current sketch` 说明，避免表格左列换行。
- Application 入口在出现时使用低频 appsection，避免和设备体检摘要抢第一屏注意力。
- 内置 CSS 移除 Status 页不再使用的 health hero 样式，减少公共 CSS 体积。

业务侧影响：

- Status 页 URL、API、字段语义和底部系统入口不变；变化只影响内置 HTML/CSS 表达。
- 业务首页若要表达“设备状态 + 关键指标 + 详细诊断”，应优先使用紧凑状态分区；大摘要只适合真正需要强提醒的业务首页，不作为基础库 Status 默认形态。

### 内置页面 UI baseline 第二轮对齐

优化：

- WiFi、Auth、OTA 和 System 工具页切换到统一的 `formpanel`、`actionpanel`、`dangerpanel` 和 `uploadpanel` 结构；低频表单、只读维护信息、普通操作和危险操作的视觉边界更清楚。
- System 页 Hostname、Footer bar、Watchdog、File log、Restart、Format LittleFS 和 Clear logs 区块统一使用 baseline 表格、表单和操作按钮样式；危险操作保留二次确认和 `POST -> 303 -> GET` 行为。
- `docs/04_web.md` 和 `docs/11_web_ui_baseline.md` 补充内置低频表单、普通操作和危险操作的复用规则，业务页面遇到同类场景应优先复用基础能力块。

业务侧影响：

- URL、POST 参数、NVS key、认证逻辑、OTA 上传流程和 System 工具语义不变。
- 业务项目如需类似“低频设置 + 当前值 + 危险维护动作”的页面，应先参考本轮内置页结构；找不到合适能力块时回到 Esp32Base 补基础能力。

### 内置页面 UI baseline 第一轮对齐

优化：

- Status、Logs、App Config 和重启等待页开始对齐统一 Web UI baseline；只读信息区、分区表、日志元信息、日志 segment tabs、App Config 字段区和保存确认区使用更统一的 panel、表格、横向溢出和提示样式。
- `docs/11_web_ui_baseline.md` 增加业务项目接入规则和基础库优化提示词模板；业务项目找不到合适页面能力块时，应优先回到 Esp32Base 评估补统一能力，而不是在业务侧复制 CSS 或打一次性样式补丁。
- `README.md` 和 `docs/04_web.md` 增加 Web UI baseline 使用入口说明，提醒业务项目优先复用基础库 helper、能力块和基础 CSS。

业务侧影响：

- 内置页面默认英文文案、URL、POST 参数、NVS key、认证逻辑和 App Config 保存语义不变。
- 业务页面继续使用 `sendHeader()`、`sendPageTitle()`、`beginPanel()`、`sendFooter()` 和现有 helper；找不到合适能力块时，按文档模板反馈基础库补能力。

### Footer bar 运行时显示模式

新增：

- Web System 页新增 Footer bar 模式设置，可在 Off、Status only、Links + status 之间切换。
- 新增 `Esp32BaseWeb::FooterBarMode`、`setFooterBarMode()`、`footerBarMode()` 和 `footerBarModeName()`，用于业务代码读取或设置底部横条模式。
- Footer bar 模式保存到 `eb_ui.footer_mode`，出厂重置会清理该配置并恢复默认 Links + status。

业务侧影响：

- 需要隐藏底部系统入口的业务页面无需自定义 CSS 绕过，可直接在 System 页面关闭底部横条或保留右侧运行摘要。
- 直达 URL 和顶部业务导航不受影响；隐藏底部横条只改变 `sendFooter()` 的页面输出。

### FileLog 支持 ERROR 模式

新增：

- `Esp32BaseFileLog` 增加 `ERROR` 模式，运行时模式收口为 `OFF`、`ERROR`、`WARN`、`INFO`；默认仍为 WARN。
- 新增构建宏值 `ESP32BASE_FILELOG_MODE_ERROR`，`ESP32BASE_EB_FILELOG_DEFAULT_MODE` 可配置为 OFF、ERROR、WARN 或 INFO，但仍不能超过 `ESP32BASE_LOG_LEVEL` 编译期上限。
- Web System 页 File log 模式设置新增 ERROR 选项，POST 参数 `mode=error` 保存后写入 `eb_log.mode` 并立即生效。

业务侧影响：

- 量产设备需要只保留错误日志时，可使用 `Esp32BaseFileLog::setMode(Esp32BaseFileLog::ERROR)` 或构建参数 `-D ESP32BASE_EB_FILELOG_DEFAULT_MODE=ESP32BASE_FILELOG_MODE_ERROR`。
- `DEBUG` / `VERBOSE` 继续不作为文件日志模式；需要现场调试时仍推荐临时切到 INFO。

## 2026-05-27

### Web UI baseline helper

新增：

- 增加轻量 Web UI helper API，用于页面标题、panel、notice、结果提示、指标网格、紧凑行、安全行内链接、单按钮 POST 表单和分页输出。
- 内置 Web CSS 改为变量化的紧凑 UI baseline，保留 `pagehead`、`panel`、`kv`、`part`、`tabs`、`quicklinks`、`logmeta`、`logframe` 等旧类名兼容。
- `examples/full_demo` 增加 UI baseline 示例页，覆盖状态概览、统计摘要、分页记录、配置编辑、操作命令、流程向导、诊断维护和访问控制状态。
- 新增 `examples/web_ui_gallery` 独立样式样例，用于集中查看和验证状态、统计、分页记录、配置、命令、流程、维护、访问控制、确认、空状态和表单页面，并提供 selftest env 便于回归。
- 紧凑行链接和分页链接改为按钮型链接样式，避免把小尺寸状态标签用作可点击控件。
- `sendPagination()` 输出补齐每页条数选择和跳页提交；`web_ui_gallery` 启用 App Config、底部系统状态栏、真实筛选控件和简单字段行内展开编辑。
- `sendPagination()` 增加当前页附近页码，并将分页控件调整为更轻量的字号；新增 `editform` 和 `fieldgrid` 表单布局类，`web_ui_gallery` 去掉重复筛选摘要并优化多字段表单布局。
- 分页控件进一步收紧高度并改为中性色；记录筛选的预设时间范围不再展示起止时间，只有自定义范围展示开始和结束时间。
- 分页右侧去掉重复的“当前第 N 页”，分页文字、页码、标签和跳转控件统一为普通字重并垂直居中。
- 紧凑行右侧增加独立动作区，状态值和动作按钮保持清晰间距；只读状态值预留动作列空间，避免贴到按钮区域最右侧；`web_ui_gallery` 配置页的“进入编辑页”修正为进入多字段表单页。
- 基础 Web CSS 改为 `/esp32base/ui.css` 独立缓存资源，`sendHeader()` 只输出 stylesheet 引用，避免每个业务页面重复内联 9KB 级样式内容；`web_ui_gallery` selftest 增加 CSS 资源和页面体积回归检查。
- 紧凑行右侧动作区改为固定值列和动作列，配置页“花坛”“3 条”等状态值与右侧按钮竖向对齐。
- 重新收敛 Web UI 字号层级：顶部主导航调整为 14px，页面标题降为 18px，section 标题降为 16px，底部系统入口降为 12px，运行摘要降为 11px。
- 底部低频系统入口进一步降低视觉权重：外层条高度、阴影、圆角和入口按钮高度收紧，避免与主导航或正文操作抢注意力。
- 页面标题按设备控制台层级降权，避免“状态概览”等页面标题压过正文状态信息；section 标题保持 16px。
- 新增 [Web UI 页面结构与样式基线](docs/11_web_ui_baseline.md)，作为页面能力、样式、换肤和调整回路的长期文档入口。

业务侧影响：

- 业务页面可继续使用 `addPage()`、`sendHeader()`、`sendFooter()` 和 `sendChunk()`；新 helper 是增量能力，不强制迁移。
- 推荐新业务页面优先使用 helper，避免复制基础库 CSS 和重复手写分页、提示、配置行等结构。
- 紧凑行内动作使用 `sendInfoRowCompactLink()` 或 `sendInfoRowCompactForm()`；自定义 HTML 使用底层 `sendChunk()` 手动输出并自行 escape 动态内容。
- 换肤走 CSS 变量覆盖，可通过 `setHeadExtraCallback()` 调整主色和背景；不新增运行时主题系统、前端框架或图表库。
- 表单和命令提交仍推荐 `POST -> 303 -> GET`，刷新页面不应重复提交。

## 2026-05-17

### Web 默认页面背景

优化：

- `sendHeader()` 输出的内置基础 CSS 将页面 `body` 背景从灰绿色改为白色，内置 Status、WiFi、Auth、OTA、Logs、System 页面以及复用基础 header/footer 的业务页默认更干净。
- 导航、页面标题、panel、footer、按钮、表单和 active 状态配色保持不变，继续通过轻量边框和阴影区分内容块。

业务侧影响：

- 公开 API、HTTP 路由、页面结构、业务 CSS 注入顺序和 `setHeadExtraCallback()` 行为不变。
- 不新增主题系统、配置项或外部 CSS 资源；需要业务定制配色时仍通过 `setHeadExtraCallback()` 注入页面级 CSS。

### Web chunked 响应收尾稳定性

修复：

- `sendResponseContent()` 不再在 `WebServer::sendContent()` 之后立刻用 `client().connected()` 判定响应断开，避免 Arduino ESP32 Core 2.x 同步 WebServer 在已写出一个约 512 B chunk 后被误标记为 `response_client_disconnected`，导致后续业务 JSON/HTML chunk 被跳过。
- `endResponse()` 在响应未标记为断开时总是尝试发送最终 0-length chunk，不再因为发送前的连接状态判断跳过 chunked 响应终止块。小 JSON、超过 512 B 的 JSON 和 HTML 分块输出都应能让 curl/浏览器正常看到响应结束。

业务侧建议：

- 已经完整生成在内存中的小 JSON 继续优先使用 `Esp32BaseWeb::sendJson(code, json)`，该路径由 Arduino WebServer 发送 `Content-Length`，不会使用 chunked transfer。
- 需要流式拼接的大 JSON、HTML、CSV 或二进制内容继续使用 `beginResponse()` / `sendChunk()` / `sendBytes()` / `endResponse()`；业务代码必须保证每个成功 `beginResponse()` 最终调用 `endResponse()`。

### Web 首屏与请求路径性能优化

优化：

- WiFi modem sleep 默认关闭：`Esp32BaseWiFi::begin()` 在启动时显式 `WiFi.setSleep(false)`，并将 `g_wifiPowerSave` 默认值改为 `false`，避免 Arduino ESP32 默认 `WIFI_PS_MIN_MODEM` 引入 100–200 ms 的 DTIM 唤醒延迟。Status 页 `Power save` 行随之默认显示 `off`。
- Web chunked 输出移除 raw `WiFiClient::write()` 快路径：实机回归确认直接绕过 `WebServer::sendContent()` 手写 `hex\r\n + payload + \r\n` 在 Arduino ESP32 Core 2.x 下会偶发挂起；本次稳定性修复将实际发送恢复为 `WebServer::sendContent()` 编码路径，并保留客户端断开检测、`yield()` 和 `response_client_disconnected` 日志。这样放弃单次 raw write 的极限优化，但避免 partial write 或底层 chunk 收尾语义导致 curl/浏览器等待到超时。
- `sendBytes()` 等任意外部缓冲走 `sendResponseContent()` 旧 3 写入路径，同样带断连检查和 `yield()`，二进制下载稳定性不变。
- 实机回归中 `setNoDelay(true)` 会让 512 B chunked 页面在弱链路下吞吐下降，Logs 等大页面更容易触发 20s 客户端超时；本次稳定性修复不再对每个 HTTP 请求强制关闭 Nagle。
- Web chunk buffer 保持 512 B 稳定发送形态；`sendProgmem()` 改为复用该 buffer，避免内置 CSS/JS 走旧 128 B 临时 buffer 产生大量小 chunk。实机验证显示 1 KB 和 1.4 KB 级 chunk payload 会偶发挂起，因此稳定性优先于扩大 chunk 的极限吞吐优化（实际段数仍受 WebServer 编码、MSS、PMTU、lwIP 调度影响）。
- Logs 页面改为小 HTML 外壳 + `/esp32base/logs/raw?segment=N` 的 `text/plain` iframe；日志正文不再逐字符 HTML escape 进主页面，raw endpoint 复用 512 B chunk buffer 流式输出原文，并保留 Basic Auth、`yield()` 和 Watchdog feed。
- PlatformIO `webota` 默认改走 `/esp32base/ota/raw` 的 `application/octet-stream` 上传，跳过 multipart 解析；浏览器页面继续使用 `/esp32base/ota` multipart 路径，显式把 `esp32base_webota_path` 设为 `/esp32base/ota` 时也保留旧兼容路径。
- Web OTA 统计口径拆分：命令行输出上传前 3 次 RSSI 取样、完成时 RSSI、client send、wait response 和端到端总耗时，并把进度行改为固定字段；`/esp32base/api/ota` 增加 `elapsedMs` 与 `averageBytesPerSecond`，用于区分客户端 socket 写入、设备端 OTA 处理和响应等待。
- 内置基础 CSS 移除 App Config 专用样式（约 700 B），改为只在 App Config 页面通过新增内部 head-injector 注入；其他 6 个内置页和业务页首屏字节数等量减少。App Config footer 入口仍由 `sendSystemLinks()` 注册，导航行为不变。

业务侧影响：

- 同样 ESP32 板型在路由器旁的常规测试场景下，Web 首屏 TTFB 和总耗时预期明显下降；实际收益取决于路由器调度、信号强度和并发请求情况，建议业务在自己实机上量首屏耗时和 free heap 做最终评估。
- 电池或弱网静默场景仍可调用 `Esp32BaseWiFi::setPowerSave(true)` 恢复 modem sleep；该 API 行为不变。
- 公开 API（`Esp32BaseWeb::sendHeader/sendFooter/sendChunk/beginResponse/beginJson/beginCsv/sendBytes/writeHtmlEscaped/writeJsonEscaped` 等）签名、HTTP 响应内容与现有路由表均不变。
- `setHeadExtraCallback()` 的业务覆盖仍在内部 head-injector 之后输出，业务 CSS 优先级不受影响。

关键边界：

- 静态 Web chunk buffer 仍为 512 B；本次 PROGMEM 发送优化不增加 chunk buffer BSS。
- WiFi modem sleep 关闭后空闲电流增加约 20–50 mA，按需通过 `setPowerSave(true)` 回退。
- 不改变 chunked transfer 编码：Arduino ESP32 WebServer 仍发 `Transfer-Encoding: chunked` 与 `Connection: close`（每次响应后关闭连接），外部 HTTP 客户端、curl、PlatformIO `webota` 行为一致。
- ESP32 实机的首屏耗时、空闲电流、弱信号 WiFi 稳定性、OTA、Logs 大页面在合入前需在目标设备上回归测试，本次仅完成编译验证。

## 2026-05-15

### Web 长响应发送稳定性

修复：

- Chunked 响应的文本缓冲、PROGMEM head、二进制块和结束块发送后都会主动 `yield()`，降低 App Config 等大 HTML/CSS 页面在 ESP32 实机上截断或短时间拖住 Web 服务的风险。
- 客户端已断开时停止继续输出当前响应，并记录 `response_client_disconnected`，避免长页面继续占用 Web handler。

### 离线业务时间与同步回填基础能力

修复：

- 修复本 boot 已输出 `time_synchronized` 后，SNTP 内部状态回到 idle/reset 导致 `/esp32base` Status 页重新显示 pending、`Esp32BaseNtp::snapshot().synced` 不能稳定保持 true 的问题。
- 首次可信同步仍由 SNTP completed 事件触发；完成后 `snapshot()`、`isRealTime()`、Status 页 Time 和 `resolveCurrentBootEvent()` 使用锁存的 `bootStartEpochSec` 与可信 epoch 判断，不再要求 `sntp_get_sync_status()` 持续等于 completed。
- `TimeSnapshot.uptimeSec` 改用 ESP-IDF 64-bit 运行时间计数源，避免 Arduino `millis()` 约 49.7 天回卷后导致离线事件回填时间跳变。
- OTA 进度百分比计算改用 64-bit 中间值，避免大分区固件上传时乘以 100 的中间结果溢出。
- FileLog 轮转 LittleFS 文件时会在多步 remove/rename 操作之间 `yield()`，并在 Watchdog 启用且当前任务尚未被其他长操作移除时临时移除 WDT 监控，降低 INFO 高流量轮转对 loop 和 Web 请求的阻塞影响。

新增：

- `Esp32BaseNtp::TimeSnapshot`，包含 `synced`、`epochSec`、`uptimeSec`、`bootId` 和 `bootStartEpochSec`，供业务项目统一记录离线事件时间。
- `Esp32Base::begin()` 会初始化当前 boot 时间会话，递增并持久化 `eb_sys.time_boot_id`；断网启动时业务仍可通过 `snapshot()` 获得当前 `bootId + uptimeSec`。
- `Esp32BaseNtp::onTimeSynced(callback)` 在本次 boot 首次可信同步时回调，业务可扫描自己的日志并回填同一 boot 的相对时间。
- `isRealTime()`、`isCurrentBootEvent()`、`canResolveCurrentBootEvent()`、`resolveCurrentBootEvent()` 明确区分当前 boot 可回填事件和历史未知时间。

调整：

- `/esp32base` Status 页 Time 行、Watchdog trip reset 写入时间、日志绝对时间切换和业务 `TimeSnapshot.synced` 使用同一个 NTP 可信同步语义，避免页面显示 synced 而业务 API 仍未同步。
- WiFi STA 连接前会把当前 Esp32Base hostname 应用为 DHCP client hostname；Web/API 保存 hostname 后仍需重启才会影响 DHCP、mDNS 和 OTA。
- README 补充 `writeBytesAt()` 固定位置覆盖的前置条件；App Config 文档明确 NVS 部分写入失败时不回滚已成功字段。
- `Esp32BaseWatchdog` 增加 `currentTaskRemovedForLongOperation()`，用于长操作嵌套时避免提前恢复其他模块移除的 WDT 状态。

关键边界：

- Esp32Base 不理解也不改写业务日志结构；业务只使用 bootId、uptimeSec、同步回调和转换 helper 自行回填。
- helper 只转换当前 boot 的相对事件；历史 boot 或未知 bootId 不会被误修正为真实日期。

## 2026-05-14

### App Config 内置业务配置页

新增：

- 新增可裁剪能力 `ESP32BASE_ENABLE_APP_CONFIG`，用于业务注册可持久化修改的应用参数，并由基础库生成内置配置页。
- 启用后必须显式设置 `ESP32BASE_APP_CONFIG_MAX_GROUPS` 和 `ESP32BASE_APP_CONFIG_MAX_FIELDS`；硬上限为 16 组、128 字段。
- 新增 `Esp32BaseAppConfig`，支持 string、int、decimal、bool、enum 字段，字段用结构体注册并绑定业务 `Esp32BaseConfig` namespace/key。
- System 页新增 `App Config` 入口，固定页面为 `GET/POST /esp32base/app-config`。
- 保存前前端显示变更核对清单；后端重新解析、校验、读取和比较，只保存实际变化字段，未变化字段不写 NVS。
- 确认清单在确认后会检测字段是否再次变化；若变化则要求重新核对，避免展示值和最终提交值不一致。
- `restartRequired` 字段保存后，未重启会话内会持续显示待重启提示、运行中旧值和已保存新值；改回旧值后提示消失。极低内存下 string/enum 旧值不可记录时会显示 `unavailable`，提示保留到重启。
- App Config 页面使用窄单列布局，数字字段使用紧凑输入宽度，单位紧贴输入框；System 页中 App Config 入口显示在首位。
- 支持字段级校验回调、页面级校验回调、逐字段 change 回调和保存 summary 回调；change 回调提供旧值和新值。
- `full_demo` 启用 App Config 示例，覆盖 string/int/decimal/bool/enum、字段校验、页面校验、重启提示和回调。
- 启用 App Config 时，底部系统导航增加 `App Config` 直达入口；System 页面原有 App Config 入口保持不变。

关键边界：

- 业务 namespace 不能使用 `eb_` 前缀；字段注册必须在 `Esp32Base::begin()` 前完成。
- decimal 是定点数，底层使用 `int32_t raw` 保存，`scale` 支持 `0..6`。
- 第一版不做单字段弹窗、多配置页、敏感字段隐藏或 CSRF token；安全边界为 Web Auth、POST only 和 Origin/Referer 同源检查。
- `ChangeCallback` 中的 text 指针只在回调期间有效；需要长期保存时由业务自行复制。

## 2026-05-13

### 冻结前诊断资产与 OTA/Web 边界收紧

调整：

- `factoryReset()` / `clearSystemConfig()` 不再清空整个 `eb_sys` namespace，只清理 `eb_sys.hostname`，保留 boot/restart/watchdog 统计和诊断 key。
- Watchdog trip 当 `wdt_trip_base` 大于 lifetime reset count 时显示 `invalid baseline`，不再把异常基线折算成 `0`。
- Reset Watchdog Trip 写入 `wdt_trip_base` / `wdt_trip_time` 后会立即回读确认，失败时返回错误提示。
- Web OTA 在 `startUpload()` 阶段失败时记录拒绝原因，后续 chunk 不再继续写入，最终 JSON 返回明确错误。
- Status 页将 `OTA headroom` 改名为 `OTA slot minus current sketch`；`Max OTA upload` 才是上传硬上限。
- 文档资源表同步刷新 ESP32 Core 2.x 7 profile、ESP32-C3 FULL 和 ESP32 Core 3.x FULL 的当前 size；历史条目中的 `OTA headroom` 指的就是改名前的同一状态页指标。
- Web 路由文档补齐 `/esp32base/tools/filelog` 和 `/esp32base/tools/logs-clear`，并标明 `/esp32base/logs/clear` 是保留直达入口。

关键边界：

- 出厂重置保护基础库诊断资产，但仍会清理 WiFi、Web Auth、FileLog 模式和 hostname 配置。
- `/esp32base/api/wifi` 仍是表单提交兼容端点，返回 303 到 HTML 页面，不是 JSON API。
- Core 3.x、实机 OTA/WDT/WiFi/AP/LittleFS/soak 仍需按发布清单继续验证。

## 2026-05-11

### FileLog 运行时模式收口

破坏性调整：

- `Esp32BaseFileLog` 改为模式 API：`setMode(OFF/WARN/INFO)`、`mode()`、`modeName()`；不再公开按 path/max/level/files 拼装启用的接口。
- 文件日志运行时只支持 OFF、WARN、INFO；DEBUG/VERBOSE 不作为文件日志模式，也不会出现在内置 Web 配置中。
- 文件日志默认值改为 `ESP32BASE_EB_FILELOG_DEFAULT_MODE`，可取 `ESP32BASE_FILELOG_MODE_OFF`、`ESP32BASE_FILELOG_MODE_WARN`、`ESP32BASE_FILELOG_MODE_INFO`。
- FileLog 持久化配置收口为 `eb_log.mode`；不读取旧的启用/等级 key，不做 NVS 迁移。
- Web System 页新增 FileLog 模式设置，保存后立即生效；Logs 页面继续只负责查看日志。
- Logs 页面在 FileLog OFF 时仍展示已有历史日志；OFF 只停止后续写入，不隐藏或删除历史内容。
- FileLog 模式变更 WARN 审计日志补充上一次模式和新模式，便于现场确认配置变化来源。
- 示例通过 `ESP32BASE_EB_FILELOG_DEFAULT_MODE=ESP32BASE_FILELOG_MODE_INFO` 设置默认 INFO 模式，不再在应用代码里重新配置 FileLog。

关键边界：

- `ESP32BASE_LOG_LEVEL` 仍是编译期最高保留日志等级；FileLog 默认模式和 Web 可选项都不能突破该上限。
- FileLog path、max bytes、rotate files、buffer、flush interval 继续是构建期资源策略，不在内置 Web 普通运维界面暴露。

### 内置 Web 导航与底部状态摘要

优化：

- Status 页重构为只读设备体检页，按 Overview、Hardware、Firmware & OTA、Network、Storage & Logs、Partition Table、Boot Reasons 分组展示。
- Status 页新增芯片型号、revision、CPU、SDK、eFuse MAC、STA MAC、AP MAC、heap max alloc、Watchdog lifetime/trip resets、OTA headroom 和完整运行时分区表。
- Web System 页新增 Watchdog trip 小计重置；只写入 `eb_sys.wdt_trip_base` 和 `eb_sys.wdt_trip_time`，保留 `eb_sys.wdt_cnt` 总累计；无可用时间时页面明确显示 `unknown (time unavailable)`。
- Watchdog trip reset 时间保存逻辑与 Status 页 Time 行使用同一可信 epoch 判断，避免页面显示 Time synced 但 trip reset time 仍写入 unknown。
- Status 页移除常驻 `OTA status` 行，保留 OTA 内部状态机和 `/esp32base/api/ota` 诊断字段；异常时仍显示 `Last OTA error`。
- eFuse MAC 按常见网络 MAC 顺序展示，便于和 STA MAC、AP MAC 直接对照调试。
- 修正 config portal 默认 AP SSID 后缀，`ESP32-Config-XXXX` 的 `XXXX` 现在取可读 eFuse MAC 的最后两个字节，而不是 `ESP.getEfuseMac()` 整数低 16 位。
- WiFi 配置支持开放 WiFi：SSID 仍必须非空，密码可为空；`clearCredentials()` 现在返回 NVS 清理真实结果，Web 清除失败会显示失败反馈。
- 分区表展示 Name、Type、SubType、Offset、Size、Role；Role 标识 running app、next OTA、app data、NVS config、OTA state、coredump 等调试语义。
- 默认系统导航改为底部紧凑区，顶部导航主要用于业务首页和业务页面。
- 默认系统入口收敛为 Status、Logs、System；WiFi、Auth、OTA 作为低频配置/维护入口收进 System 页，但保留原直达 URL。
- System 页入口使用见名知义的短名称：WiFi Setup、Web Auth、Firmware OTA。
- `BUILTIN_TOOLS` 默认标签从 `Tools` 改为 `System`，更准确表达设备级维护页面职责。
- 底部状态摘要保留 `Free heap:`，并新增简写 `Up:` 和 `RSSI:`，使用更小字号显示，便于快速判断运行时长和 WiFi 信号。
- 内置 Web 默认正文调整为 14px，标题为 18px/16px/15px，按钮、输入框、footer 和日志区域同步收紧为设备控制台尺度。

关键边界：

- 不新增主题系统或配置 API；业务仍可通过 `setHeadExtraCallback()` 覆盖样式。
- WiFi、Auth、OTA 页面职责不合并进 System，只在 System 页提供入口，避免低频配置表单和危险操作混在一个长页面中。

### Hostname 编译期默认值与持久化管理

新增/调整：

- 移除 `Esp32Base::setHostname()` 公开 API，不再支持应用代码运行期设置 hostname，避免 mDNS、ArduinoOTA 和 Web 展示出现半生效状态。
- 新增 `ESP32BASE_DEFAULT_HOSTNAME` 编译期默认 hostname，未配置时为 `esp32base`。
- 新增 `Esp32Base::defaultHostname()` 和 `Esp32Base::isValidHostname()`。
- `Esp32Base::begin()` 在 Config 初始化后读取 hostname：先使用 `ESP32BASE_DEFAULT_HOSTNAME`，再用合法的 `eb_sys.hostname` 覆盖。
- hostname 校验规则固定为 1-32 位小写字母、数字和短横线，不能以短横线开头或结尾，不能包含 `.` 或 `.local`。
- Web System 页新增 Hostname 设置区，显示当前值、默认值、已保存值和是否需要重启。
- 新增 `GET /esp32base/api/hostname` 与 `POST /esp32base/api/hostname`。
- Web/API 保存 hostname 只写入 `eb_sys.hostname`，不热切换当前运行时 hostname；重启后 mDNS、ArduinoOTA、状态页和 API 完整使用新名称。
- `factoryReset()` 清理 `eb_sys.hostname` 后，重启恢复 `ESP32BASE_DEFAULT_HOSTNAME`。

推荐接入：

```ini
build_flags =
  -D ESP32BASE_DEFAULT_HOSTNAME=\"esp32-fan\"
```

业务侧用途：

- 多台同类设备运行同一业务固件时，可先用构建默认 hostname 提供稳定出厂身份，再通过 Web/API 为每台设备保存唯一 hostname。
- 业务项目不需要自行保存/恢复 hostname，也不需要分别处理 mDNS、OTA 和 Web 展示。

关键边界：

- Hostname 是启动期设备身份，不支持运行期热切换。
- Web/API 修改成功后必须重启，才会影响 mDNS `<hostname>.local` 和 ArduinoOTA/espota。
- 出厂重置不会删除业务 namespace；只清除基础库 namespace，其中 `eb_sys.hostname` 会被清除。

### 日志、出厂重置与 Config 审计收口

新增/调整：

- `Esp32BaseLog` 新增 `setSerialLevel()` / `serialLevel()`，Serial 输出等级和 FileLog 写入等级解耦。
- `runtimeLevel()` 仍控制日志是否生成并分发给 sink/FileLog；`serialLevel()` 只控制 `Serial.println()`。
- 量产设备可设置 `Esp32BaseLog::setSerialLevel(Esp32BaseLog::NONE)` 关闭串口，同时保持 FileLog WARN/ERROR 或 INFO 正常写入。
- 新增 `Esp32BaseConfig::factoryReset()`。
- 新增单项清理 API：`clearWifiConfig()`、`clearWebAuthConfig()`、`clearSystemConfig()`、`clearLogConfig()`。
- `clearLibraryNamespaces()` 现在等价于 `factoryReset()`。
- namespace 不存在时清理返回成功，不创建空 namespace，也避免底层 `NOT_FOUND` 噪声。
- 文档明确 `enableConfigAudit(true)` / `enableConfigReadAudit(true)` 可在 `Esp32Base::begin()` 前调用；需要覆盖基础库初始化读写时必须在 begin 前开启。

推荐接入：

```cpp
Esp32BaseLog::setSerialLevel(Esp32BaseLog::NONE);
Esp32BaseFileLog::setMode(Esp32BaseFileLog::WARN);
```

```cpp
Esp32BaseConfig::factoryReset();
Esp32BaseConfig::flushAll();
Esp32BaseSystem::restart("factory reset");
```

关键边界：

- 如果 `ESP32BASE_LOG_LEVEL=ESP32BASE_LOG_NONE`，日志宏会被编译掉，FileLog 也收不到日志。
- `factoryReset()` 不自动重启、不格式化 LittleFS、不删除 FileLog 日志文件内容、不清理业务 namespace。
- `enableConfigReadAudit(true)` 噪声较多，读取审计主要在 DEBUG 编译等级下观察。

## 2026-05-10

### Config 小型结构化元数据

新增：

- `Esp32BaseConfig` 新增 `setBlob()` / `getBlob()` / `setBlobDeferred()`，用于一次保存和读取最大 256 字节的小型固定大小元数据。
- 新增 `setPod<T>()` / `getPod<T>()` / `setPodDeferred<T>()` 模板薄封装，要求类型为 trivially copyable 且 standard-layout，适合保存 `head/count/next_id` 这类环形文件元数据。
- blob 同步写入会做写前比较；旧值长度和内容完全相同时返回 true 并跳过 NVS 写入。
- blob deferred 写入支持 pending read-through、同 key 合并、相同 pending 跳过，以及 `flushAll()` 强制落盘。

业务侧用途：

- 业务项目的 RecordStore/EventStore 可把 `head`、`count`、`next_id` 合并为一个 POD metadata key，避免每条记录或事件 append 后分别写 3 个 NVS key。
- blob API 只保存小型结构化元数据；记录正文、事件环形文件和高频日志仍应由业务文件或 FS 能力承载。

文档：

- `docs/03_api.md` 补充 blob/POD API、256 字节上限、namespace/key 校验、失败返回、相同值跳过写入、deferred/flushAll 语义和示例。
- `full_demo` 示例增加 POD metadata 的 deferred 写入和 pending read-through 示例。

## 2026-05-09

### Web POST 轻量同源防护与 OTA 健壮性

修复：

- 内置危险 POST 增加轻量 `Origin` / `Referer` host 校验；存在这些头时必须与请求 `Host` 同源，缺失时继续放行，保证 curl、PlatformIO `webota` 和简单脚本可用。
- 覆盖 WiFi 保存/清除、Auth 保存、重启、Tools 操作、Logs clear、Web OTA upload/done；校验失败返回 403，不执行副作用。
- Web OTA 开始上传前检查下一 OTA 分区容量；固件超过 app slot 时给出明确错误，不进入写入流程。
- OTA 写入过程检查累计写入不能超过声明总大小；客户端伪造过小 size 后继续写入时会 abort。
- 设备端 OTA 日志增加上传摘要，成功、失败或 abort 时输出耗时、已上传容量和平均速度，例如 `upload_summary duration=13.49s uploaded=1020.2 KB average=75.7 KB/s`。
- `Update.begin()` 失败时记录底层 Update 错误字符串，减少只看到通用失败信息的调试成本。
- NTP 同步判定同时检查 SNTP 同步状态和最小 epoch，避免设备曾设置过 RTC 时误判已完成网络对时。
- `scripts/esp32base_webota.py` 新增 `esp32base_webota_upload_timeout`，默认 600 秒；原 `esp32base_webota_timeout` 继续用于预检和普通请求。
- `full_demo`、`web_logs_ota` 和 `net_runtime` 示例补充 ESP32-S3 8MB env，避免 S3/8MB 板使用 ESP32 4MB 分区表浪费空间。

文档：

- 内置 Web 导航标签枚举移除旧 Reboot 历史别名；系统工具页统一使用 `BUILTIN_TOOLS`，并移除旧 `/esp32base/reboot` 路由。
- 移除顶层 `Esp32BaseLog.h` / `Esp32BaseConfig.h` 转发头，公开入口统一为 `#include <Esp32Base.h>`。
- `webota` 配置只读取 env 中的 `custom_esp32base_webota_*`、`[esp32base_webota]` 公共段和环境变量，不再兼容非 `custom_` env option。
- 明确多核/多任务项目推荐单系统服务任务模型：`Esp32Base::begin()`、`Esp32Base::handle()`、`Esp32BaseConfig`、Web、Bus 固定在同一个 loop/system task 中调用。
- 明确 `Esp32BaseConfig` 当前不是线程安全 API；业务 task 应通过 FreeRTOS queue/flag/ring buffer 投递配置变更，再由 loop/system task 调用 `setXxx()` 或 `setXxxDeferred()`。
- 明确 `setAuthEnabled(false)` 会完全开放内置 HTTP 路由，包括 OTA、重启、Tools 和配置提交，只适合受控调试网络。
- 补充 `Esp32BaseWeb::redirectSeeOther()` API、`Esp32BaseFileLog` 最小示例，以及示例默认 ESP32 4MB 分区、S3/C3/8MB 板型需选择匹配 env/分区表。

设计边界：

- 本轮不改变个人项目/调试优先安全模型：INFO 明文日志、默认弱口令、开放配置 AP、OTA 无签名、NVS 明文、可关闭 Web Auth 仍按既定设计保留。
- 本轮不给 `Esp32BaseConfig` 加 mutex，也不引入完整 CSRF token 系统；多任务安全通过明确单任务调用模型和业务侧队列投递实现。

## 2026-05-08

### 启动严格模式与 Captive DNS 生命周期修复

修复：

- `ESP32BASE_STRICT_OPTIONAL_BEGIN=1` 现在会对 FS、FileLog、Watchdog、Sleep、Health、WiFi 等所有可选模块生效；任一可选模块 begin 失败都会使 `Esp32Base::begin()` 返回 false。
- Captive Portal DNS 只在 WiFi 处于 `CONFIG_PORTAL` 时运行；设备切回 STA 或离开配网页后会停止 DNS server，避免旧的 captive DNS 状态残留。
- `full_demo` 自测用例同步当前 Tools 页面结构，避免启用自测时因过期 CSS 类名误报失败。

验证：

- 新增 `scripts/check_trim_symbols.py`，用于在 `examples/basic` 构建后检查最终 ELF 是否包含被禁用 profile 的重模块符号。

### 内置 Web 页面视觉统一

优化：

- 内置 Web 默认正文统一为 14px，标题层级统一为 18px / 16px / 15px。
- 页面最大宽度提升到 1040px，顶部导航、正文 panel 和底部系统入口使用一致的设备控制台布局。
- Status、WiFi、Auth、OTA、Logs、System 页面统一使用 `h1` 页面标题、白色 panel、简洁按钮、文本输入框和中性色配色。
- System 页面把标题、说明和危险操作按钮收进同一个 panel，避免标题和操作区域错位。
- 页面标题改为独立 header panel，与正文 panel 的内边距和视觉起点对齐。
- Logs 页面移除清空按钮，只负责查看日志；`Clear logs` 维护动作移到 System 页面。
- Logs 页面输出大日志时周期性 yield/feed Watchdog，降低长日志请求触发看门狗的风险。

关键边界：

- 只调整内置默认 HTML/CSS，不新增主题系统或配置 API。
- 业务项目仍可通过 `setHeadExtraCallback()` 注入 CSS 覆盖默认视觉。

### 快速命令行 Web OTA

新增：

- Esp32Base 提供可复用 PlatformIO extra_script，注册 `webota` target，可通过现有 HTTP Web OTA 接口上传固件。
- `webota` 使用 `/esp32base/ota`、Web Basic Auth、`X-Firmware-Size` 和默认自动 SHA256 校验。
- `webota` 默认 64 KB 分块上传，并在上传前通过状态接口预检连接和认证，减少反复烧录耗时和错误密码等待。
- `webota` 日志显示友好容量、开始/结束时间、用时、平均速度，并按约 5% 粒度输出进度。
- 支持 IP 地址上传，不依赖 mDNS。

业务侧用途：

- 业务项目无需复制脚本或自建临时 target；配置地址和认证后可直接运行：

```sh
pio run -t webota
```

推荐接入：

```ini
extra_scripts =
  post:path/to/Esp32Base/scripts/esp32base_webota.py

custom_esp32base_webota_host = 192.168.2.112
custom_esp32base_webota_user = admin
custom_esp32base_webota_password = admin
```

关键边界：

- `espota` 是 ArduinoOTA 标准协议，适合标准工具链兼容；`webota` 是 Esp32Base 提供的 HTTP 上传方式，通常更适合反复快速烧录。
- `webota` 复用现有 Web OTA 页面/API，不改变浏览器 Web OTA，也不影响 ArduinoOTA/espota。
- 失败会区分连接失败、认证失败、设备返回错误和上传中断。

### ArduinoOTA / espota 命令行 OTA

新增：

- 启用 `ESP32BASE_ENABLE_OTA` 时默认启用 `ESP32BASE_ENABLE_ARDUINO_OTA`，支持 PlatformIO `upload_protocol = espota`。
- ArduinoOTA 使用 `Esp32Base::hostname()`，可通过 `<hostname>.local` 和标准端口 3232 上传。
- espota 认证密码复用当前生效的 Web Auth 密码；即使关闭 Web Auth，espota 仍要求密码。
- Web OTA 与 espota 共用 OTA 状态和 Watchdog、NVS deferred flush、WiFi power save、FileLog 资源保护策略，避免同时写 flash。

业务侧用途：

- 业务项目无需再各自写 PlatformIO 脚本包装 `/esp32base/ota`，可直接使用 `pio run -t upload --upload-port <hostname>.local`。
- 不需要命令行 OTA 的项目可显式设置 `ESP32BASE_ENABLE_ARDUINO_OTA=0`，保留原 Web OTA。

关键边界：

- 非 OTA profile 不新增 ArduinoOTA 能力，不改变现有裁剪边界。
- espota 只有密码没有用户名，因此用户名不参与命令行 OTA 认证。
- Web OTA 继续支持可选 SHA256；ArduinoOTA/espota 使用协议内建 MD5。

推荐接入：

```ini
upload_protocol = espota
upload_port = esp32base-full.local
upload_flags =
  --auth=admin
```

## 2026-05-07

### 启动流程 DEBUG 诊断

优化：

- `DEBUG` 编译级别下，启动流程输出 `begin_start`、模块 `module_begin/module_ready`，以及 Web/NTP/mDNS/OTA 延迟启动或停止原因。
- FS mount 成功、Web route 注册准备、OTA ready 分区信息增加 DEBUG 诊断日志。
- 旧 Web Auth 存储缺少 `auth_pass` 时，启动时静默回退默认认证，不再触发 `Preferences.cpp ... auth_pass NOT_FOUND` 底层错误日志。
- `full_demo` 内置 OTA 导航标签统一显示为 `OTA`，不再显示 `Update`。
- `/esp32base/ota` 上传成功后，页面提示设备重启，并在短暂等待后跳转到当前配置的首页。
- `/esp32base/ota` 上传失败时留在 OTA 页面，显示失败原因，并允许重新上传。
- 新增 `Esp32BaseWeb::setHeadExtraCallback()`，业务项目可在 `sendHeader()` 输出顶部导航前注入 CSS。
- 顶部业务导航自动为当前匹配项输出 `active` class，嵌套路由按最长 path 前缀匹配。
- 基础库默认链接样式改为更中性，普通链接不再默认渲染成强蓝色按钮。
- Web 默认配色改为简洁中性色，导航和 tabs 的 active 状态使用浅绿灰背景和深绿灰文字，不再使用蓝色主色。
- 新增 `Esp32BaseFs::format()`；`full_demo` 在首次调试遇到 LittleFS 未格式化导致 FileLog 启用失败时，会格式化 FS 并重试启用 INFO 文件日志。
- 内置系统首页默认命名改为 `Status`；原 `Restart` 入口升级为 `Tools` 维护页，用于承载重启和格式化 LittleFS 等维护操作。

业务侧用途：

- 应用使用 DEBUG 串口日志时，可以直接确认启动顺序、模块初始化进度和条件启动原因。
- 旧测试设备升级后，缺少 `auth_pass` 的 NVS 记录不会污染启动日志。
- OTA 成功后浏览器会自动回到设备首页，便于确认新固件启动状态。
- OTA 失败后可直接看到失败原因并重试，不需要手动刷新页面。
- 业务项目可继续使用 `addPage()` / `addNavItem()` / `sendHeader()`，同时把业务 CSS 提前放进 head，避免页面刷新时导航先闪成基础库默认样式。
- 业务项目即使不注入 CSS，也会得到低侵入、设备控制台风格的默认导航。
- `full_demo` 首次烧录后即使 LittleFS 尚未初始化，也能自动恢复文件日志页面。
- `Tools` 页面提供显式维护动作；格式化 LittleFS 可用于调试期恢复未初始化或损坏的文件系统。

关键边界：

- 这些 DEBUG 日志不在默认 INFO 构建中输出。
- 文件日志模式独立于串口编译日志等级；`full_demo` 使用 INFO 文件日志便于观察，业务长期运行建议按需要保持 WARN。
- 不新增高频 loop、HTTP 请求或 NTP pending DEBUG 日志。
- `setHeadExtraCallback()` 回调只应输出 `<style>`、`<meta>` 等 head 内容，不应输出 body 内容。
- 默认样式只提供基础可读性，不新增主题系统或颜色配置 API。
- 基础库默认不会自动格式化 FS；`format()` 会清除 LittleFS 文件，仅由示例或应用在明确可接受数据丢失时调用。
- `Format LittleFS` 会删除日志和所有 LittleFS 文件，但不会清除 WiFi、Web Auth 或 NVS 配置；`Tools` 页面统一承载维护操作。
- Tools 页格式化成功后会明确提示，并重新加载 FileLog 模式；FileLog 会在格式化后重新创建日志目录，避免后续写入触发底层 VFS 目录不存在错误。
- 格式化 LittleFS 会输出 WARN 级维护日志，记录请求来源、format/mount/FileLog reload 结果；FileLog 目录或 segment 准备失败也会输出 WARN。
- FileLog 切换模式前会先 flush 现有 buffer，避免启动阶段 boot session 多行日志在示例或维护操作重新加载 FileLog 模式时丢失尾部。
- 普通 `GET` 页面慢请求日志降为 DEBUG；非 GET 操作慢请求仍保持 WARN，避免维护/写操作耗时被忽略。
- 配置审计中 `changed=no result=skipped`、读取审计和 deferred 排队日志降为 DEBUG；实际写入成功/flush 成功仍为 INFO，失败为 WARN/ERROR。
- 普通页面 Basic Auth 校验成功降为 DEBUG；认证缺失、无效或密码错误仍为 WARN，启动认证加载和认证修改继续保持 INFO。
- 业务页面/导航注册日志降为 DEBUG，减少示例 INFO 文件日志中的启动噪音。
- Tools 页面维护操作改为轻量分组布局，避免 `Restart device` 按钮和 `Format LittleFS` 标题贴近。
- `POST /esp32base/tools/reboot` 响应会把浏览器历史替换为 `/esp32base/tools?restarting=1`，避免刷新重复提交；重启请求输出 `restart_requested source=tools` WARN 日志。
- OTA 上传增加 INFO 生命周期日志：`upload_start`、10% 阶段 `upload_progress`、`upload_success`；进度带 `%`，字节数使用 KB/MB/B 友好展示，失败仍输出 ERROR。

推荐接入：

- 业务项目需要启动诊断时，编译开启 `ESP32BASE_LOG_LEVEL=ESP32BASE_LOG_DEBUG`；文件日志模式按设备用途单独选择，示例观察可用 INFO，长期运行建议 WARN。

### full_demo 日志等级调整

优化：

- `examples/full_demo` 编译日志等级改为 DEBUG，便于串口观察调试信息。
- `examples/full_demo` 文件日志默认模式保持 INFO，便于在 `/esp32base/logs` 观察示例运行和 Web/OTA 行为。

业务侧用途：

- 示例串口调试信息更完整，同时 `/esp32base/logs` 中可查看 INFO 级别的示例运行日志。

关键边界：

- 只调整 `full_demo` 示例；库默认日志等级和其他示例不变。

推荐接入：

- 业务项目如需类似观察体验，可编译开启 `ESP32BASE_LOG_LEVEL=ESP32BASE_LOG_DEBUG`，并按需要将 FileLog 设为 INFO；长期运行设备建议使用 WARN。

### 系统首页容量显示简化

优化：

- `/esp32base` 系统首页的 Heap、Flash、FS、File log 容量改为只显示 KB/MB/B 人性化格式，不再重复 raw bytes。

业务侧用途：

- 设备首页更易读，避免 raw bytes 与 `193.63 KB` 这类重复信息挤占页面宽度。

关键边界：

- 只调整内置系统首页展示；状态 API 保留 raw `bytes` 字段，人工页面继续优先显示人性化值。

推荐接入：

- 业务项目无需改接入代码，更新 Esp32Base 后刷新 `/esp32base` 即可看到新展示。

### NTP 未同步日志降噪

优化：

- 移除基于 `isTimeSynced()` 状态查询产生的周期性 `ntp_sync_pending` WARN。
- NTP 未同步状态不再重复输出 WARN 或 DEBUG。

业务侧用途：

- 网络或 NTP 服务器暂时不可用时，应用日志不会被未同步状态刷屏。

关键边界：

- `ntp_client_started`、`time_synchronized`、`time_mapping` 和日志时间戳切换仍保持 INFO 输出。
- NTP 同步判断仍使用 `ESP32BASE_NTP_SYNC_MIN_EPOCH`。
- 只有接入明确的单次同步失败事件时，才应为该失败事件输出 WARN。

推荐接入：

- 业务项目无需配置；通过系统页或 API 查看当前 NTP 是否已同步。

### WiFi 与 Web Auth INFO 明文凭据日志

优化：

- WiFi 在 INFO 日志中输出 SSID 和明文密码，覆盖表单提交、凭据设置和 STA 连接。
- Web Auth 在 INFO 日志中输出认证用户名和明文密码，覆盖默认认证设置、保存认证和 Basic Auth 请求校验。
- Web Auth 启动加载认证时输出 `auth_loaded user=<user> password=<pass> source=stored|default`。
- Web Auth 明文密码持久化到 `eb_web.auth_pass`，认证校验直接比较明文密码。
- Basic Auth 请求日志改为每个 HTTP 请求最多输出 1 条，避免 OTA 上传分块时重复刷屏。

业务侧用途：

- 应用项目开发和现场调试时，可直接从日志确认 WiFi 凭据和 Web 认证凭据。

关键边界：

- Web Auth 持久化保存明文密码；缺少 `auth_pass` 的旧认证记录按无效存储处理并回退默认认证。
- WiFi 凭据按既有配置策略保存到 NVS。
- HTML、JSON 和 API 响应仍不输出 Web Auth 明文密码；WiFi 配置页仍按既有策略回显 WiFi 密码。

推荐接入：

- 业务项目无需额外开启 DEBUG；保持 INFO 日志即可看到 WiFi 和 Web Auth 明文凭据。

### Health 普通 tick 日志降频

优化：

- `DEBUG health tick loopMax=...` 默认从每 30 秒一次降为每 30 分钟最多一次。
- 新增 `ESP32BASE_HEALTH_DEBUG_LOG_INTERVAL_MS`，默认 1800000ms；设为 0 可关闭普通 DEBUG tick 日志。

业务侧用途：

- DEBUG health tick 不进入文件日志模式，不会污染现场 INFO 文件日志。

关键边界：

- `health.tick` bus 事件仍按 `ESP32BASE_HEALTH_TICK_INTERVAL_MS` 默认每 30 秒发布。
- `WARN health loop_slow...` 仍在 30 秒窗口超过 `ESP32BASE_HEALTH_LOOP_WARN_MS` 时立即输出。

推荐接入：

- 业务项目无需绕过 Health；默认配置即可减少日志噪声。需要完全静默普通 tick 时，在 build flags 中设置 `ESP32BASE_HEALTH_DEBUG_LOG_INTERVAL_MS=0`。

### Logs 页面 segment 标签大小显示

优化：

- Logs 页面顶部 `current-0`、`history-N` 标签中的文件大小改为只显示 KB/MB/B 人性化值，不再同时显示 raw bytes。
- Logs 页面文件日志状态信息改为 14px 小字号，`Buffer`、`Max per file`、`Max total`、`Segments` 中的字节数也只显示 KB/MB/B 人性化值。
- Logs 页面文件日志状态信息改为紧凑 label/value 表格；segment 标签中的文件名和大小拆成两个视觉字段，避免 `history-2 0 B` 被误读。
- Logs 页面文件日志状态信息放入普通 panel，和页面标题、日志内容区域保持一致的左侧留白。
- 内置页面默认正文为 14px，导航为 14px，页面标题收敛到 18px/16px/15px 层级，footer 为 13px，整体更接近设备维护控制台。

业务侧用途：

- 日志文件切换标签更简洁，避免 raw bytes 与 `8.95 KB` 这类重复信息挤占移动端宽度。

关键边界：

- 只调整 Logs 页面顶部 segment 标签的展示文本；状态页和 API 中需要精确容量的位置保留 raw `bytes` 字段。

推荐接入：

- 业务项目无需改接入代码，更新 Esp32Base 后刷新 `/esp32base/logs` 即可看到新展示。

## 2026-05-06

### Fs 按偏移二进制读写

新增：

- `Esp32BaseFs::readBytesAt(const char* path, uint32_t offset, uint8_t* out, size_t maxLen, size_t* readLen)`
- `Esp32BaseFs::writeBytesAt(const char* path, uint32_t offset, const uint8_t* data, size_t len)`

业务侧用途：

- 可直接通过 Esp32Base Fs 实现二进制定长日志分页读取。
- 可直接通过 Esp32Base Fs 实现固定容量环形日志覆盖写入。
- 业务项目不需要 include `LittleFS.h` 或 Arduino `File`。

关键行为：

- `readBytesAt()` 支持从指定 offset 读取，读到 EOF 前允许短读，并通过 `readLen` 返回实际读取长度。
- `writeBytesAt()` 只覆盖已有文件内容，不创建文件、不扩展文件、不填洞。
- FS 未 ready、path 非绝对路径、文件不存在、offset 越界、写越界或底层读写失败时返回 `false`。

推荐接入：

- 业务先用 `writeBytes()` 或分块 `appendBytes()` 初始化固定容量文件。
- 后续按记录大小计算 offset，用 `readBytesAt()` 分页读取，用 `writeBytesAt()` 覆盖环形槽位。

### Web 当前请求方法 API

新增：

- `Esp32BaseWeb::METHOD_UNKNOWN`
- `Esp32BaseWeb::currentMethod()`
- `Esp32BaseWeb::isMethod(Esp32BaseWeb::Method method)`
- `Esp32BaseWeb::currentMethodName()`

业务侧用途：

- 可用 `METHOD_ANY` 注册同一路径，例如 `GET /api/faucet/config` 读取配置、`POST /api/faucet/config` 保存配置。
- handler 内可可靠区分 GET 和 POST。
- 业务项目不需要直接访问底层 `WebServer`。

关键行为：

- handler 上下文中，GET 返回 `METHOD_GET`，POST 返回 `METHOD_POST`。
- handler 外返回 `METHOD_UNKNOWN`。
- `isMethod(METHOD_ANY)` 在有效 handler 请求中返回 true。
- `METHOD_ANY` 仍用于路由注册，不作为实际请求方法返回。

推荐接入：

```cpp
void handleConfigApi() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_GET)) {
        Esp32BaseWeb::sendJson(200, "{\"ok\":true}");
        return;
    }
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        Esp32BaseWeb::redirectSeeOther("/config?saved=1");
        return;
    }
    Esp32BaseWeb::sendText(405, "method not allowed");
}

Esp32BaseWeb::addRoute("/api/faucet/config", Esp32BaseWeb::METHOD_ANY, handleConfigApi);
```

### Web 通用 chunked 响应与 CSV 导出

新增：

- `Esp32BaseWeb::beginResponse(int code, const char* contentType, const char* filename = nullptr)`
- `Esp32BaseWeb::beginText(int code)`
- `Esp32BaseWeb::beginCsv(int code, const char* filename = nullptr)`
- `Esp32BaseWeb::endResponse()`
- `Esp32BaseWeb::sendBytes(const uint8_t* data, size_t len)`
- `Esp32BaseWeb::writeCsvEscaped(const char* text)`

业务侧用途：

- 可在应用 handler 内输出 CSV、JSONL、文本、二进制或其他自定义 content type 的 chunked 响应。
- CSV 导出可通过 `beginCsv(200, "records.csv")` 自动发送 `Content-Type: text/csv; charset=utf-8` 和下载用 `Content-Disposition`。
- 业务项目不需要访问底层 `WebServer`，也不需要复制 Esp32BaseWeb 内部 chunked 实现。

关键行为：

- `beginResponse()`、`beginText()`、`beginCsv()` 只能在 handler 请求上下文中成功；handler 外返回 false 并记录 WARN。
- contentType 为空、超过 63 字节或包含控制字符时返回 false。
- filename 非空时只允许字母、数字、`.`、`_`、`-`，非法时返回 false。
- `sendChunk()` / `sendBytes()` 只能在已开始的 chunked 响应中输出；响应结束必须调用 `endResponse()`。

推荐接入：

```cpp
void handleRecordsCsv() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    if (!Esp32BaseWeb::beginCsv(200, "records.csv")) {
        return;
    }
    Esp32BaseWeb::sendChunk("id,name\r\n");
    Esp32BaseWeb::sendChunk("1,");
    Esp32BaseWeb::writeCsvEscaped("zone-a");
    Esp32BaseWeb::sendChunk("\r\n");
    Esp32BaseWeb::endResponse();
}
```

### Web 业务优先导航与首页模型

新增：

- `Esp32BaseWeb::HomeMode`
- `Esp32BaseWeb::SystemNavMode`
- `Esp32BaseWeb::BuiltinPage`
- `Esp32BaseWeb::addNavItem(const char* path, const char* title)`
- `Esp32BaseWeb::setDeviceName(const char* name)`
- `Esp32BaseWeb::setHomePath(const char* path)`
- `Esp32BaseWeb::setHomeMode(Esp32BaseWeb::HomeMode mode)`
- `Esp32BaseWeb::setSystemNavMode(Esp32BaseWeb::SystemNavMode mode)`
- `Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BuiltinPage page, const char* label)`

业务侧用途：

- 可声明业务页面是设备主界面，基础库 WiFi、OTA、Logs、System 是系统工具。
- 可设置 `/` 和 `/esp32base` 使用基础库首页、业务首页优先或融合首页。
- 可把系统工具放在顶部、底部或底部紧凑系统工具区。
- 可覆盖 Home/WiFi/OTA/Logs/System/Auth 标签，适配中文业务项目。

关键行为：

- 未配置时保持 Esp32Base 默认首页和默认导航。
- `addPage()` 注册业务页面并进入业务导航；`addNavItem()` 只注册导航项，不注册路由。
- 配置业务首页后，导航品牌链接指向业务首页，业务主导航不会重复显示同一个首页入口。
- `HOME_APP` 和 `HOME_COMBINED` 下，设备根路径 `/` 都进入业务首页；`HOME_COMBINED` 仅保留 `/esp32base` 为融合首页。
- 内置页面和业务页面复用同一套 `sendHeader()` / `sendFooter()` 导航框架。
- `SYSTEM_NAV_SECTION` 使用底部小字系统入口，与 `Free heap`、`Up`、`RSSI` 同行展示；移动端允许系统入口自然换行，避免挤压和横向滚动。

推荐接入：

```cpp
Esp32BaseWeb::setDeviceName("Faucet");
Esp32BaseWeb::setHomePath("/status");
Esp32BaseWeb::setHomeMode(Esp32BaseWeb::HOME_COMBINED);
Esp32BaseWeb::setSystemNavMode(Esp32BaseWeb::SYSTEM_NAV_SECTION);
Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_HOME, "状态");
Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_WIFI, "网络");
Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_LOGS, "日志");
Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_TOOLS, "工具");
Esp32BaseWeb::addPage("/status", "状态", handleStatusPage);
Esp32BaseWeb::addPage("/config", "配置", handleConfigPage);
```

### Web input style scope

优化：

- `sendHeader()` 输出的基础 CSS 不再使用过宽的 `input:not([type=submit]):not([type=button])` 选择器。
- 默认输入框样式仅作用于文本类 input：未声明 type 的 input、`text`、`password`、`number`、`email`、`url`、`tel`、`search`。
- `checkbox`、`radio`、`file`、`range`、`color`、`hidden` 等非文本控件保持浏览器原生尺寸和行为。

业务侧用途：

- 业务页面复用 `Esp32BaseWeb::sendHeader()` / `sendFooter()` 时，可以直接使用 checkbox/radio 表单项，不需要写高优先级 CSS 覆盖基础库样式。
- 内置页面未来新增非文本控件时，也不会被默认文本输入框样式污染。

推荐接入：

```html
<label><input type="checkbox" name="beep" value="1"> 蜂鸣器</label>
<label><input type="radio" name="mode" value="auto"> 自动</label>
```

### Runtime/Core robustness cleanup

优化：

- `Esp32BaseFs::listDir()` 遍历目录时显式关闭每个 `File` 句柄，并在非目录路径上关闭 root 句柄后返回 false，避免目录遍历积累底层文件句柄。
- `Esp32BaseNtp::isTimeSynced()` 不再使用旧的硬编码 `1600000000` 阈值，新增 `ESP32BASE_NTP_SYNC_MIN_EPOCH`，默认 `1700000000UL`。
- `Esp32BaseWatchdog::begin(timeoutMs)` 明确要求 `timeoutMs >= 1000`；更小值返回 false 并记录 WARN，避免 Arduino ESP32 2.x 下秒级参数被截断为 0。
- `Esp32BaseBus::publish()` 先快照本次匹配订阅，再调用 callback；callback 内允许 `unsubscribe()`，已取消的后续订阅不会继续收到当前事件，新订阅不会收到当前正在发布的事件。
- `Esp32BaseConfig` 字符串读取和写前比较不再使用 Arduino `String`，改用固定 scratch buffer 和 NVS 字符串读取，减少配置读取造成的 heap 碎片风险。
- `Esp32BaseSystem::restart()` 和 `Esp32BaseSleep::deepSleepUs()` 去除冗余的前置 `Esp32BaseFileLog::flush()`，保留记录 restart/sleep 日志后的最终 flush。
- `docs/01_architecture.md` 的 begin/handle 顺序补充 FileLog 位置，与当前代码一致。

业务侧影响：

- 目录列举、事件订阅变更、NTP 同步判断、Watchdog 参数和 Config 字符串读取的边界行为更明确。
- 应用如覆盖 NTP 同步判断，可在 include 前定义 `ESP32BASE_NTP_SYNC_MIN_EPOCH`。

### Web system page and log viewer diagnostics

优化：

- `/esp32base` 系统页不再只显示 `Ready`，改为展示固件、profile、hostname、uptime、boot count、reset/wake reason、heap、flash，以及当前 profile 可用的 WiFi/IP/RSSI、FS、FileLog、NTP、OTA 状态。
- Logs 页面改为单文件展示，默认显示最新 `current-0`，可通过顶部标签切换 `history-1`、`history-2` 等历史文件，避免多个日志文件都有内容时页面过大。
- Logs 页面保留所有 history/current segment 标签和大小，包括 0 字节文件，便于准确判断轮转文件状态。
- Logs 页面只做 HTML escape，不折叠、不省略、不规整空白行，页面展示忠实反映文件内容。
- Core Log 不再规整 message 中的 CR/LF；库内部需要单行日志时由调用点传入单行 message。
- 新增 boot session 启动诊断块，FileLog 初始化后输出 `boot` tag 多行短日志，包含 `boot_count`、reset/wake reason 及中文说明、固件、版本、build、profile、hostname、heap、flash，避免单行过长被日志缓冲截断。
- 新增 `Esp32BaseSystem::bootCount()`，返回 `uint32_t` 启动会话计数；NVS 使用 unsigned key `eb_sys.boot_cnt` 存储，每次固件启动加 1。
- 新增 `Esp32BaseSystem::resetReasonText()` / `wakeReasonText()`，Web 系统页和 `/esp32base/api/status` 同时输出 reason 原始值与中文说明。
- 新增 Web Basic Auth 配置管理能力：`setDefaultAuth()`、`authUser()`、`verifyAuth(user, pass)`、`saveAuth()`、`resetAuth()`。
- Web Auth 认证优先级明确为：已保存认证 > 应用默认认证 > 库默认 `admin/admin`；`setDefaultAuth()` 不覆盖用户已保存认证。
- 新增内置 `/esp32base/auth` 认证管理页面，并加入系统工具导航；`BUILTIN_AUTH` 可用于本地化标签，中文推荐“认证”。
- Web Auth 可持久化到 `eb_web.auth_user`、`eb_web.auth_pass`，明文密码用于启动日志、认证校验和现场调试。
- Web Auth 更新后立即生效；浏览器缓存旧 Basic Auth 时，后续请求会重新触发认证。
- OTA 与 Auth 配置解耦：OTA route 按 profile/OTA 编译条件注册；Web Auth 开启时受 Basic Auth 保护，Web Auth 关闭时无密码保护。
- `clearLibraryNamespaces()` 会清理 `eb_web`，恢复出厂时同步清除持久化 Web Auth。

业务侧用途：

- 业务项目访问 `/esp32base` 可以直接看到设备基础状态，不需要另写基础诊断页。
- 现场查看日志时，日志页更容易判断轮转文件顺序、空 segment 和异常空白来源。
- 业务项目可用 `bootCount()` 判断固件累计启动会话次数，并结合 `resetReason()` / `wakeReason()` 判断本次启动来源。
- 业务项目可直接链接 `/esp32base/auth`，让用户修改设备认证账号/密码，不需要自行保存基础库鉴权数据。
- 业务项目可每次启动调用 `setDefaultAuth()` 提供设备默认认证，用户保存过认证后基础库会优先使用已保存认证。
