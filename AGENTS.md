# Esp32Base 协作规范

本文件面向 AI coding agent 和维护者，只记录本项目的长期协作规则。通用沟通风格、代码编辑安全和工具使用规则不放在这里。

## 1. 项目定位和依据

- `Esp32Base` 是面向 ESP32 / ESP32-S3 / ESP32-C3、基于 Arduino ESP32 Core 的可裁剪基础库，不是 Web 管理平台、云框架或大型组件系统。
- 当前仍处于试用和首版定型阶段，不为旧 API、旧 key、旧行为保留迁移层或兼容 fallback。
- 判断设计和实现时，以 `README.md`、`docs/`、现有代码和示例为准；历史设计、评审记录、临时评估文件和过程流水不作为实现依据。
- 涉及芯片、Arduino Core、PlatformIO、OTA、分区表、安全/Auth、存储和协议细节时，优先依据项目文档和上游权威资料；无法核实时明确说明不确定性。

## 2. 架构边界

- 保持 Core 最小：Core 只承担日志、NVS 小配置、系统诊断和生命周期，不引入 WiFi、Web、OTA、LittleFS、Watchdog、Sleep 或业务语义。
- 依赖方向必须符合 `docs/01_architecture.md`：Application -> Esp32Base -> Update -> Web -> Network -> Runtime -> Core，不允许低层依赖高层、同层强耦合或循环依赖。
- Profile 和裁剪规则以 `docs/02_profiles.md` 为准；新增能力必须评估是否影响 profile、依赖检查、LDF、示例和 map 符号裁剪。
- Web、OTA、FS、FileLog、App Events、App Config 等重能力必须保持可裁剪；未启用模块不应编译、链接或初始化。
- LittleFS 的 `/esp32base/**` 是基础库管理命名空间；业务文件应使用 `/app/**`、`/data/**` 或项目自定义目录。

## 3. 修改原则

- 修改前先读对应模块实现、`README.md` 和相关 `docs/`，不要只凭局部代码猜设计意图。
- 新需求、bug、优化和业务接入反馈先判断是否应由 Esp32Base 承担；不是基础库问题时，说明依据、业务侧建议，以及是否需要补充文档或示例。
- 是基础库问题时，按最终清晰设计推进；不要做临时补丁、业务侧绕行方案或“先这样以后再说”的实现。
- 只改完成目标必需的内容，不顺手重构相邻模块，不清理无关历史问题。
- 修改 public API、默认行为、profile、Web 页面、日志、存储、OTA/Auth、安全策略或示例接入方式时，同步更新 README、相关 `docs/` 和示例。
- `CHANGELOG.md` 不作为项目契约；新 API、行为边界、业务用途和推荐接入方式应写入 README 或对应 `docs/`。

## 4. 验证要求

- 能构建的改动至少运行最相关的 PlatformIO 示例构建；选择覆盖本次改动风险最大的 env。
- 根目录 native harness 可用：
  - `pio test -e native_web_harness`
  - `pio test -e native_config_harness`
- Profile/裁剪/LDF 相关改动优先验证 `examples/basic` 的相关 env；涉及 CORE/NET/WEB/FULL 裁剪时，结合 `scripts/check_trim_symbols.py` 和 `docs/09_release_checklist.md` 检查。
- Web UI 改动优先验证 `examples/web_ui_gallery`；App Config/完整集成优先验证 `examples/full_demo`；App Events 改动优先验证 `examples/app_events_demo`；OTA 改动优先验证 `examples/web_logs_ota` 或 FULL 示例。
- 涉及 Web、OTA、WiFi、FileLog、Sleep、Watchdog 的改动，优先做 ESP32 实机验证；无法实机验证时在回复中明确说明缺口和风险。
- 影响发布包内容时，运行 `platformio pkg pack .`，并结合 `scripts/check_release_hygiene.py`、`docs/09_release_checklist.md` 确认应包含和不应包含的文件。
- 烧录 ESP32 时优先只刷 app 分区并保留 NVS/WiFi；不要擅自切换电脑 WiFi。
- 不留下生成 tarball、临时包、串口 monitor、后台 dev server 或其他干扰物。

## 5. 高影响操作

- 删除、覆盖、格式化 LittleFS、清 NVS、出厂重置、串口烧录、OTA、推送、发布、改账号/权限/认证配置前，先说明影响范围并取得确认。
- 不输出、提交或记录密钥、token、真实 Web Auth 密码、WiFi 密码和隐私配置；发现敏感信息时提醒风险。
- 完成业务反馈后，给出可直接转发给反馈项目的简洁回复，并提醒对方以 README 和对应 `docs/` 为最新能力边界。
