# Esp32Base 协作规范

本文件面向 AI coding agent 和后续维护者，是本仓库的工程协作约束。修改本项目时优先遵守本文件；详细 API 和背景说明以 `docs/` 与 `README.md` 为准，但代码、示例、文档必须保持一致。

## 1. 总原则

- 首版尚未对外发布，不考虑历史包袱。
- 项目实现以最佳方案、最优实现和最佳实践为标准，不考虑历史兼容性。
- 不保留旧 key、旧 API、旧行为的迁移读取、兼容层或兼容式 fallback。
- 不做临时补丁、绕行方案或“先打补丁以后再说”的实现；即使影响大量代码、示例、文档或业务接入方案，也只按最终最优设计推进。
- 学习 `Esp8266Base` 已验证的文件日志、内置 Web、表单交互和现场诊断体验，但不照搬 ESP8266 平台实现。
- Core 保持最小，Runtime/FS/Web/OTA 分层清晰，重能力必须可裁剪。
- 每次改动都要同时考虑代码、示例、文档、测试和实机使用体验。

## 2. 外部项目反馈处理流程

- 本库会在业务项目中持续试用；来自业务项目的任何反馈，无论是新需求、bug、优化建议还是使用困难，都必须先做整体评估。
- 评估时必须判断：这是否是 Esp32Base 的问题；是否属于基础库应该提供的能力范围；是否会影响架构边界、裁剪、示例、文档、测试或实机体验。
- 如果是本库问题或本库应承担的能力，必须给出符合本库架构的优化方案，并按最终最优实现推进。
- 如果不是本库问题，或超出基础库能力范围，必须说明依据、建议业务侧如何处理，以及是否需要补充文档或示例来减少误解。
- 新增或优化能力完成后，必须同步更新根目录 `CHANGELOG.md`，记录新 API/行为、业务侧用途、关键返回行为和推荐接入方式，方便正在接入本库的项目及时使用最新能力。
- 反馈完成后，必须给出一段可直接转发给反馈项目的简洁回复，说明已完善的能力、可用的新 API/行为和业务侧后续应如何接入。

## 3. 架构边界

- `Esp32BaseLog` 属于 Core，只负责 Serial 日志、编译期等级裁剪、runtime level、日志格式化和固定小容量 sink 分发。
- Core Log 不 include LittleFS，不依赖 `Esp32BaseFs`，也不承担文件日志逻辑。
- `Esp32BaseFileLog` 属于 Runtime/FS 可选模块，依赖 `Esp32BaseLog` 和 `Esp32BaseFs`，通过 Log sink 接收格式化后的日志行。
- `CORE` 和默认 `NET` 不链接 LittleFS；启用 FS 的 profile 默认可用文件日志。
- 所有 restart 必须走 `Esp32BaseSystem::restart(reason)`；所有 deep sleep 必须走 `Esp32BaseSleep`。
- restart、deep sleep、OTA success、rollback restart、WDT restart 前必须 flush config 和 file log。

## 4. 文件日志规范

- 默认路径：`/logs/eb_app.log`。
- 默认轮转：`4 x 32KB = 128KB`。
- 默认文件等级：WARN。
- 启用 FS 的示例程序显式把文件日志设为 INFO，方便观察。
- INFO/DEBUG 仅在 file level 降到 INFO/DEBUG 后写文件，并使用 `1KB / 2s` 缓存。
- WARN/ERROR 立即写文件。
- 不做文件日志节流，不做 dropped count。
- 文件满后轮转 current、`.1`、`.2`、`.3`。
- 追加或轮转异常时优先恢复 current 写入能力，允许极端情况下丢少量历史；Serial 日志不受影响。

## 5. Config / NVS 规范

- 配置后端使用 ESP32 NVS，不采用 LittleFS KV。
- deferred 队列默认固定容量为 `8`，宏为 `ESP32BASE_EB_CONFIG_PENDING_MAX`。
- deferred 队列使用固定数组，同 namespace/key 合并，写后立即读必须可见。
- `handle()` 每轮最多 flush 一条 due item；OTA 上传期间暂停普通 deferred flush；`flushAll()` 不受暂停影响。
- 写前必须比较，无变化不写 NVS。
- 库内部 namespace 全部使用 `eb_` 前缀，例如 `eb_wifi`、`eb_sys`、`eb_log`。
- namespace 已表达模块归属，key 不重复模块前缀，例如 `eb_wifi.ssid`、`eb_wifi.pass`、`eb_sys.rst_cnt`、`eb_log.level`。
- 应用不得使用 `eb_` 前缀作为自己的 NVS namespace。

## 6. Web 与页面规范

- 内置 Web 使用轻量单栏布局，移动端友好，顶部导航，简洁按钮和表单。
- 页面输出优先使用 chunked/streaming，避免整页 `String` 拼接。
- 所有用户、设备、日志、文件内容进入 HTML 前必须 escape；JSON 输出必须 escape。
- 表单统一 `once(form)`；危险操作统一 `confirm()`；POST 成功优先 303 redirect。
- `/esp32base`、内置顶栏和业务页面顶栏必须能到达业务主页面。
- 业务页面必须通过 `Esp32BaseWeb::addPage(path, title, handler)` 注册，并显式提供短标题。
- 不保留 `addPage(path, handler)`，不做 path 推导标题。
- `addApi()` 和底层 `addRoute()` 不进入业务入口列表，避免 API 或隐藏路由污染导航。
- Logs 页面必须 Basic Auth，读取前调用 `Esp32BaseFileLog::flush()`，日志内容必须 HTML escape。
- Logs clear 使用 POST + confirm + once + 303。
- Web slow request 默认阈值为 250ms，必须记录 method、uri、elapsed。

## 7. Health / 诊断规范

- Health 只负责周期性采样、loop 周期统计和发布 `health.tick` 事件，不重复包装 heap、WiFi、FS 等模块状态。
- Health tick 普通周期日志为 DEBUG，不得以 INFO 刷屏污染 `/esp32base/logs`。
- 每个 tick 窗口内统计一次最大 loop 间隔。
- 当 `loopMax > ESP32BASE_HEALTH_LOOP_WARN_MS` 时输出 WARN `loop_slow`。
- `ESP32BASE_HEALTH_LOOP_WARN_MS` 默认 3000ms。
- `loopPeriodMaxMs()` 返回启动以来最大 loop 间隔，不随 tick 窗口清零。

## 8. WiFi / OTA / 生命周期规范

- 无 WiFi 凭证时进入 config portal。
- 有凭证但连接失败时保持 STA，不自动开 AP。
- 提交新 WiFi 凭证必须同步保存；保存失败不切换 STA。
- INFO 日志不输出明文密码；DEBUG/VERBOSE 可用于现场调试。
- OTA route 只有应用显式 `setAuth()` 后才注册；未授权不得调用 `Update.begin()`。
- 提供 SHA256 时必须先校验通过，再调用 `Update.end(true)`。
- OTA 上传期间暂停普通 deferred flush，移除/恢复 Task WDT，关闭/恢复 WiFi power save。
- OTA success 必须通过 `Esp32BaseSystem::restart("ota success")` 重启。

## 9. 示例与文档规范

- `examples/basic` 是维护者矩阵项目，继续覆盖 profiles、芯片代表构建和裁剪哨兵。
- 独立 PIO 示例用于用户打开即用，例如 `examples/full_demo`、`examples/web_logs_ota`、`examples/net_runtime`。
- 非 FS profile 示例不得强行启用 FS，避免破坏裁剪。
- 修改 public API、默认行为、profile、Web 页面或日志策略时，必须同步更新 README、相关 docs 和示例。
- 文档不应描述旧兼容路径；首版发布前以最终实现为准。

## 10. 验证与操作规范

- 修改前先读现有实现和文档，优先遵循本库已有模式。
- 能构建的改动至少运行相关 PlatformIO 示例构建。
- 涉及 Web、OTA、WiFi、FileLog、Sleep、Watchdog 的改动，优先做 ESP32 实机验证。
- ESP32-S3 / ESP32-C3 实机验证在没有硬件时可以明确记录为未做，不用假装完成。
- 48 小时 soak 没有执行时必须明确记录，不能用短测替代。
- 需要烧录 ESP32 时，优先只刷 app 分区以保留 NVS/WiFi 配置。
- 不擅自切换电脑 WiFi；需要配网时由用户操作。
- 不要留下生成 tarball、临时包、串口 monitor、后台 dev server 等干扰物。
- 可能影响发布包内容时，运行 `pio pkg pack .` 并确认 `.inc`、示例、分区表被包含，`.pio`、`.cache`、`design-history` 等不被包含。
- 涉及 PlatformIO LDF 的改动必须用外部最小应用验证：只通过 `lib_deps = file:///.../Esp32Base` 引用本库、只 `#include <Esp32Base.h>`、启用目标 profile。
- FULL / FS / Web / OTA profile 不得要求业务代码额外 include Arduino ESP32 framework 内置库；CORE 裁剪以最终 ELF/map 不链接重库符号为准。
