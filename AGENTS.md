# Esp32Base 协作规范

本文件面向 AI coding agent 和维护者。当前项目处于业务项目试用和基础能力快速完善阶段；修改本库时优先遵守本文件。详细 API、背景和示例以 `docs/`、`README.md`、`CHANGELOG.md` 为准，代码、示例、文档必须保持一致。

## 1. 项目阶段与演进原则

- 当前是试用阶段，不是兼容维护阶段；所有变更都不考虑历史包袱。
- 后续工作主要来自新需求、bug、优化和业务接入反馈；无论哪一类，都以最佳方案、最优实现和最佳实践为准。
- 不保留旧 key、旧 API、旧行为迁移层或兼容式 fallback；需要改名、改结构、改行为时直接按最终清晰设计推进。
- 不做临时补丁、业务侧绕行方案或“先这样以后再说”的实现；最佳方案影响较大时，先说明影响范围和收益，再按最终设计实施。
- Core 保持最小，Runtime/FS/Web/OTA 分层清晰，重能力必须可裁剪。

## 2. 反馈与需求处理流程

- 本库会在业务项目中持续试用；任何反馈都必须先整体评估，不默认接受，也不默认推给业务项目。
- 评估必须判断：是否是 Esp32Base 的问题；是否属于基础库能力范围；是否影响架构边界、裁剪、示例、文档、测试或实机体验。
- 是本库问题或应由本库承担时，给出符合架构的优化方案，并按最终最优实现推进。
- 不是本库问题或超出范围时，说明依据、业务侧建议，以及是否需要补充文档/示例减少误解。
- 方案设计不要被现有实现、旧命名或已写文档绑架；允许为了正确模型同步调整代码、示例、文档和业务接入方式。
- 新增、修复或优化能力完成后，必须更新根目录 `CHANGELOG.md`，记录新 API/行为、业务用途、关键边界和推荐接入方式。
- 反馈完成后，给出可直接转发给反馈项目的简洁回复，并提醒对方及时查看 `CHANGELOG.md` 获取最新能力边界。

## 3. 架构与存储边界

- `Esp32BaseLog` 属于 Core，只负责 Serial 日志、编译期等级裁剪、runtime level、格式化和固定小容量 sink 分发；不 include LittleFS，不依赖 FS，不承担文件日志。
- `Esp32BaseFileLog` 属于 Runtime/FS 可选模块，依赖 Log 和 FS，通过 Log sink 接收格式化日志行。
- `CORE` 和默认 `NET` 不链接 LittleFS；启用 FS 的 profile 默认可用文件日志。
- 所有 restart 走 `Esp32BaseSystem::restart(reason)`；所有 deep sleep 走 `Esp32BaseSleep`。
- restart、deep sleep、OTA success、rollback restart、WDT restart 前必须 flush config 和 file log。
- Config 后端使用 ESP32 NVS，不采用 LittleFS KV；库内部 namespace 全部使用 `eb_` 前缀，应用不得使用 `eb_`。
- NVS key 不重复模块前缀，例如 `eb_wifi.ssid`、`eb_sys.boot_cnt`、`eb_log.level`、`eb_web.auth_hash`。
- deferred 配置队列固定数组、同 namespace/key 合并、写后立即读可见；OTA 上传期间暂停普通 deferred flush，`flushAll()` 不受暂停影响。

## 4. 文件日志与诊断

- 默认文件日志路径 `/logs/eb_app.log`，默认轮转 `4 x 32KB`，默认文件等级 WARN；启用 FS 的示例可显式设为 INFO。
- INFO/DEBUG 仅在 file level 降到对应等级后写文件，并使用 `1KB / 2s` 缓存；WARN/ERROR 立即写文件。
- 不做文件日志节流，不做 dropped count；轮转异常时优先恢复 current 写入能力，Serial 日志不受影响。
- Logs 页面读取前调用 `Esp32BaseFileLog::flush()`，内容只做 HTML escape，不折叠、不省略、不规整空白行。
- boot session 日志使用 `boot` tag，包含 unsigned `boot_count`、reset/wake reason 及说明、固件、profile、hostname、heap、flash。
- Health 只负责周期采样、loop 周期统计和 `health.tick`，普通 tick 日志为 DEBUG；`loop_slow` 才输出 WARN。

## 5. Web 与页面规范

- 内置 Web 使用轻量单栏布局，移动端友好，顶部导航，简洁按钮和表单。
- 页面输出优先使用 chunked/streaming，避免整页 `String` 拼接。
- 所有用户、设备、日志、文件内容进入 HTML 前必须 escape；JSON 输出必须 escape。
- 表单统一 `once(form)`；危险操作统一 `confirm()`；POST 成功优先 303 redirect。
- `/`、`/esp32base`、内置导航和业务页面导航必须能到达业务主页面。
- 业务页面必须通过 `Esp32BaseWeb::addPage(path, title, handler)` 注册并显式提供短标题；不保留 `addPage(path, handler)`。
- `addApi()` 和 `addRoute()` 不进入业务入口列表，避免 API 或隐藏路由污染导航。
- Web slow request 默认阈值 250ms，必须记录 method、uri、elapsed。
- 默认 input CSS 只作用于文本类控件，不污染 checkbox、radio、file、range、color 等控件。

## 6. Web Auth / WiFi / OTA

- Web Auth 认证优先级：已保存认证 > 应用默认认证 > 库默认 `admin/admin`。
- 应用使用 `Esp32BaseWeb::setDefaultAuth(user, pass)` 设置默认认证；用户已保存认证时不得被默认认证覆盖。
- 业务修改认证优先链接内置 `/esp32base/auth`；自建页面使用 `verifyAuth(currentUser, currentPass)` + `saveAuth(newUser, newPass)`。
- Web Auth 密码不保存明文，只保存 salted SHA-256；日志、HTML、JSON、自检输出不得包含密码、hash、salt、Authorization header 或 base64 原文。
- 无 WiFi 凭证时进入 config portal；有凭证但连接失败时保持 STA，不自动开 AP。
- 提交新 WiFi 凭证必须同步保存；保存失败不切换 STA；INFO 日志不输出明文 WiFi 密码。
- OTA route 按 profile/OTA 编译条件注册，不依赖应用是否设置默认认证。
- Web Auth 开启时 OTA 复用 Basic Auth，未授权不得调用 `Update.begin()`；Web Auth 关闭时 OTA 无密码保护。
- OTA 提供 SHA256 时必须先校验通过，再调用 `Update.end(true)`；OTA success 必须通过 `Esp32BaseSystem::restart("ota success")` 重启。
- OTA 上传期间暂停普通 deferred flush，移除/恢复 Task WDT，关闭/恢复 WiFi power save。

## 7. 示例、文档与验证

- `examples/basic` 是维护者矩阵项目，覆盖 profiles、芯片代表构建和裁剪哨兵。
- 独立 PIO 示例用于用户打开即用，例如 `examples/full_demo`、`examples/web_logs_ota`、`examples/net_runtime`。
- 非 FS profile 示例不得强行启用 FS，避免破坏裁剪。
- 修改 public API、默认行为、profile、Web 页面、日志或 OTA/Auth 策略时，必须同步更新 README、相关 docs、示例和 `CHANGELOG.md`。
- 文档不描述旧兼容路径；首版发布前以最终实现为准。
- 修改前先读现有实现和文档，优先遵循本库已有模式；能构建的改动至少运行相关 PlatformIO 示例构建。
- 涉及 Web、OTA、WiFi、FileLog、Sleep、Watchdog 的改动，优先做 ESP32 实机验证；S3/C3 或 48 小时 soak 未做时必须明确记录。
- 烧录 ESP32 时优先只刷 app 分区保留 NVS/WiFi；不擅自切换电脑 WiFi。
- 不留下生成 tarball、临时包、串口 monitor、后台 dev server 等干扰物。
- 可能影响发布包内容时，运行 `pio pkg pack .` 并确认 `.inc`、示例、分区表被包含，`.pio`、`.cache`、`design-history` 等不被包含。
- 涉及 PlatformIO LDF 的改动必须用外部最小应用验证：只通过 `lib_deps = file:///.../Esp32Base` 引用本库、只 `#include <Esp32Base.h>`、启用目标 profile。
