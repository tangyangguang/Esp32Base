# 发布检查清单

## 1. 文档冻结

发布前必须确认：

- README 已更新。
- docs 全部与实现一致。
- Profile 表与代码一致。
- API 文档与头文件一致。
- 资源表已填写。
- 实机测试结果已记录。
- [已知限制](10_known_limitations.md) 已更新。

## 2. 架构冻结

确认：

- Core 不包含 Bus / WiFi / Web / OTA / Fs / Watchdog。
- Profile 数量固定为 7 个。
- 不引入静态模块注册表。
- 不引入模板 policy 架构。
- 不引入未列入边界的新模块。

## 3. 编译检查

必须通过：

- 7 个 profile。
- ESP32 / ESP32-S3 / ESP32-C3。
- Arduino Core 2.x / 3.x。

检查：

- warnings。
- firmware size。
- text / data / bss。
- map 符号。
- 非法 `ESP32BASE_PROFILE` 必须编译失败。
- 错误依赖组合必须编译失败，例如启用 `OTA` 但关闭 `WEB`。
- 启用 `OTA` 时默认启用 `ARDUINO_OTA`；显式 `ESP32BASE_ENABLE_ARDUINO_OTA=0` 必须可关闭命令行 OTA。

## 4. 发布包检查

必须通过：

- `platformio pkg pack .` 成功。
- 发布包包含可选模块 `.inc` 实现文件。
- 发布包包含 `partitions/` 推荐分区表。
- 推荐分区表的 `app0` 偏移必须和 PlatformIO / Arduino 上传地址一致；默认应为 `0x10000`。
- 发布包包含示例依赖哨兵 `examples/basic/src/deps_*.cpp`。
- 发布包包含独立 PIO 示例 `examples/full_demo`、`examples/web_ui_gallery`、`examples/web_logs_ota`、`examples/net_runtime`。
- 发布包不包含 `design-history/`。
- 发布包不包含 `.pio/`、`.cache/`、`idf_component.yml` 等构建生成物。

## 5. 裁剪检查

必须证明：

- CORE 不链接 WiFi/WebServer/Update/LittleFS。
- NET 不链接 WebServer/Update/LittleFS，除非显式启用 FS。
- WEB 不链接 Update。
- FS 关闭时 FileLog 不拉入 LittleFS。
- 关闭 Bus/Fs/Health 不产生静态对象。
- 外部最小应用仅 `#include <Esp32Base.h>` 并启用 FULL profile 时，不额外声明 framework 内置库也必须编译通过，包括 ArduinoOTA。

## 6. OTA 检查

必须通过：

- Web Auth 开启时，未认证不能写 flash。
- Web Auth 关闭时，OTA route 仍注册且无密码保护。
- espota 正确密码上传成功。
- espota 错误密码上传失败且设备仍可访问。
- `pio run -t webota` 通过 HTTP Web OTA 上传成功。
- `webota` 错误 Web Auth 上传失败且设备仍可访问。
- Web OTA 与 espota 不得同时写 flash。
- SHA256 正确路径成功。
- SHA256 错误路径失败。
- `Update.end(true)` 只在 SHA256 通过后调用。
- Watchdog remove/restore。
- Config deferred flush pause/resume。
- WiFi power save off/restore。
- 中途断电不变砖。
- rollback 可用。
- 上传页进度显示正确。
- 上传页进度容量只显示 KB/MB/B 人性化值；状态/API JSON 保留 raw `bytes` 字段。

## 7. 配网检查

必须通过：

- 无凭证进入 AP config portal。
- 有凭证但连接失败时不自动进入 AP。
- Web 在 config portal 下启动。
- DNS 拦截生效。
- 所有 DNS 查询通配到 AP IP。
- iOS / Android / macOS / Windows 可进入配网页。
- WiFi 配置页拒绝空 SSID，允许空密码用于开放 WiFi。
- WiFi 配置页拒绝超长 SSID / 密码。
- WiFi 配置页回显 SSID / 密码。
- 提交凭证后切换 STA。
- 错误密码进入 backoff。
- 路由器恢复后重连。

## 8. 存储检查

必须通过：

- namespace/key 长度校验。
- readonly 读取不创建 namespace。
- 写前比较。
- deferred delay。
- `flushAll()`。
- deferred 写后立即读返回 pending 新值。
- restart 前全部落盘。
- NVS 写满返回 false。
- `factoryReset()` 清理 `eb_wifi`、`eb_web`、`eb_log`、`eb_ui` 和 `eb_sys.hostname`。
- `factoryReset()` 保留 `eb_sys` 中的 boot/restart/watchdog 统计诊断 key。
- 单项清理 API 只影响对应配置范围；`clearSystemConfig()` 只清 hostname，不清统计诊断 key。
- namespace 不存在时出厂重置返回成功，不创建空 namespace。
- `factoryReset()` 不误删业务 namespace，不格式化 LittleFS，不删除 FileLog 日志文件内容。
- `clearLibraryNamespaces()` 与 `factoryReset()` 行为一致。
- `enableConfigAudit(true)` / `enableConfigReadAudit(true)` 在 `Esp32Base::begin()` 前开启时能覆盖基础库初始化配置读写。
- 未配置 `ESP32BASE_DEFAULT_HOSTNAME` 时默认 hostname 为 `esp32base`。
- 合法 `ESP32BASE_DEFAULT_HOSTNAME` 生效；非法默认 hostname 回退 `esp32base` 并输出 WARN。
- 合法 `eb_sys.hostname` 覆盖构建默认 hostname；非法持久化 hostname 被忽略并输出 WARN。
- `factoryReset()` 后重启清除 `eb_sys.hostname`，恢复构建默认 hostname。

## 9. 文件日志检查

必须通过：

- FS profile 默认启用 WARN 文件日志。
- 非 FS profile 不强行启用 FileLog。
- 默认路径为 `/logs/eb_app.log`。
- 默认轮转为 `4 × 32KB`。
- ERROR 仅在 FileLog 模式为 ERROR/WARN/INFO 后写文件；WARN 仅在 WARN/INFO 后写文件；INFO 仅在 INFO 后写文件；DEBUG/VERBOSE 不能配置为文件日志模式。
- INFO 使用 `1KB / 2s` 缓存。
- Web System 页只能设置 Off/ERROR/WARN/INFO，非法 POST 值必须失败。
- Web System 页切换 FileLog 模式时，WARN 审计日志必须包含上一次模式和新模式。
- FileLog 配置模式开启但运行期因 FS 写入故障停写时，Status、Logs、System 页必须显示 `write fault`，并说明已有日志可能仍可读取，不能显示成 `disabled`。
- FileLog 模式为 OFF 时，Logs 和 System 页必须醒目显示 `disabled`，并说明新日志不会写入。
- FileLog OFF 后 Logs 页面仍能查看已有历史 segment。
- `setSerialLevel(NONE)` 后 Serial 不输出，但 FileLog 仍按当前模式写入。
- `setRuntimeLevel(NONE)` 后 Serial 和 FileLog 都停止。
- WARN/ERROR 立即写入。
- 文件满后轮转 current、`.1`、`.2`、`.3`。
- clear 幂等，清空后 current 可继续写入。
- restart、deep sleep、OTA success、rollback restart 前 flush。

## 10. 文件系统检查

必须通过：

- auto-format 默认关闭。
- 首次挂载失败不 halt。
- 开启 auto-format 后可格式化。
- 读写删文件正常。
- 二进制读写正常。
- append 正常。
- `readBytes()` / `readBytesAt()` 支持分页读取，文件不存在和 offset 越界返回失败，EOF 短读返回实际长度；未到 EOF 却读出 0 字节必须返回失败。
- `writeFile()` / `writeBytes()` / `appendFile()` / `appendBytes()` 写后大小校验正常，非空写入后末端可读。
- `writeBytesAt()` 支持已有文件固定位置覆盖，文件不存在和写越界返回失败，不隐式扩展文件；覆盖后文件大小不变且覆盖范围可读。
- FS 已满或存在不可读文件时，Web 诊断返回错误不应触发 task WDT 重启。
- `/esp32base/fs?manage=1` 对 `unreadable` 文件仍必须提供单文件删除入口，但不能提供下载入口。
- 单文件删除失败后应尝试截断为 0；如果因此释放可见文件占用，页面不能只显示 `delete_failed`。
- 清理文件后如果 FileLog 处于写入故障保护，应重新加载当前 FileLog 模式以便恢复写入。
- listDir / mkdir / rmdir 正常。
- FS 失败不影响 WiFi/Web。

## 11. Watchdog / Sleep 检查

必须通过：

- Watchdog enable/feed。
- 故意阻塞触发 reset。
- reset count 跨 boot 记录。
- Sleep 前 flushAll。
- deep sleep 必须走统一生命周期流程。
- ESP32-C3 不支持 wake source 返回 false。

## 12. Web 检查

必须通过：

- Basic Auth。
- 状态 API。
- chip API。
- firmware API。
- Hostname API。
- WiFi 配置 API。
- Logs 页面。
- Logs clear POST + confirm + once + 303。
- Logs clear 回到页面后显示成功/失败提示。
- FS/FileLog 不可用时 Logs 页面显示 `File log unavailable`。
- 日志内容 HTML escape。
- 默认 Web 首页和导航开箱可用。
- 业务优先导航可设置 device name、home path、home mode、system nav mode。
- Footer bar 可切换 Off、Status only、Links + status；Off 不输出底部横条，Status only 只输出运行摘要，Links + status 输出系统入口和运行摘要。
- `addPage()` / `addNavItem()` 注册的业务入口进入业务导航且不重复业务首页入口。
- 内置 Status/WiFi/OTA/Logs/Tools/Auth 标签可覆盖，用于本地化。
- Tools 维护页中的重启和格式化 FS 按钮都有二次确认。
- Tools 维护页可保存 hostname，显示当前值、默认值、已保存值和重启需求；保存后不热切换当前运行时 hostname。
- 启用 `ESP32BASE_ENABLE_APP_CONFIG` 后，System 页显示 App Config 入口，`/esp32base/app-config` 可按 group 展示业务参数。
- App Config 注册校验覆盖非法 namespace、重复 `ns/key`、非法长度/范围/step/decimal scale/enum option 和超容量。
- App Config POST 必须服务端重新校验；字段级 validator 和页面级 validator 失败时零写入、零 change 回调。
- App Config 保存只写变化字段，未变化字段不写 NVS；旧页面 revision 提交被拒绝。
- App Config 修改 `restartRequired` 字段后，未重启会话内重新进入页面仍显示待重启提示、运行中旧值和已保存新值；改回旧值后提示消失。
- App Config callback 能拿到正确旧值和新值；decimal raw、bool、enum 值语义正确。
- 格式化 FS 等破坏性维护操作必须输出 WARN 级日志，记录发起、结果和关键恢复步骤。
- 自定义路由 begin 前注册。
- 自定义路由 Web ready 后注册。
- JSON escape。
- 内置页面大字节数只显示 KB/MB/B 人性化值；状态/API JSON 保留 raw `bytes` 字段。
- 慢请求日志。
- 长 handler 限制已文档化。

## 13. Examples 检查

必须通过：

- `examples/basic` 继续覆盖 profile/芯片/Core 版本矩阵。
- `examples/basic` 的 `deps_*.cpp` 哨兵继续验证裁剪。
- `examples/full_demo` 可在自身目录 `pio run`。
- `examples/full_demo` 覆盖 App Config string/int/decimal/bool/enum、字段级校验、页面级校验、重启提示和回调。
- `examples/web_ui_gallery` 可在自身目录 `pio run`。
- `examples/web_ui_gallery` 覆盖 Web UI baseline 的状态、统计、分页记录、配置、命令、流程、维护、访问控制、确认、空状态和表单页面，并提供 selftest env。
- `examples/web_logs_ota` 可在自身目录 `pio run`。
- `examples/net_runtime` 可在自身目录 `pio run`。
- 所有启用 FS 的示例通过 `ESP32BASE_EB_FILELOG_DEFAULT_MODE=ESP32BASE_FILELOG_MODE_INFO` 将文件日志默认模式设为 INFO。

## 14. Soak 检查

必须完成 48 小时 soak：

- FULL profile。
- ESP32 / ESP32-S3 / ESP32-C3 各一台。
- 高频 deferred NVS 写入。
- 周期 Web 状态查询。
- 周期 `health.tick` bus 事件。
- Health tick 默认只输出 DEBUG；超过 loop 阈值才输出 WARN。
- 路由器掉电恢复。
- 周期读取 Logs 页面。
- 无意外重启、卡死、heap 持续退化或重连失败。

## 15. 发布后维护规则

发布后只接受：

- bug fix。
- Arduino Core 兼容修复。
- 芯片适配修复。
- 文档修正。
- 测试补充。

不接受：

- 新增大型模块。
- 大规模架构调整。
- 改变 profile 语义。
- 破坏公开 API 的非必要改动。
