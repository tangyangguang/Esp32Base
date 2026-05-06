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

## 4. 发布包检查

必须通过：

- `platformio pkg pack .` 成功。
- 发布包包含可选模块 `.inc` 实现文件。
- 发布包包含 `partitions/` 推荐分区表。
- 推荐分区表的 `app0` 偏移必须和 PlatformIO / Arduino 上传地址一致；默认应为 `0x10000`。
- 发布包包含示例依赖哨兵 `examples/basic/src/deps_*.cpp`。
- 发布包包含独立 PIO 示例 `examples/full_demo`、`examples/web_logs_ota`、`examples/net_runtime`。
- 发布包不包含 `design-history/`。
- 发布包不包含 `.pio/`、`.cache/`、`idf_component.yml` 等构建生成物。

## 5. 裁剪检查

必须证明：

- CORE 不链接 WiFi/WebServer/Update/LittleFS。
- NET 不链接 WebServer/Update/LittleFS，除非显式启用 FS。
- WEB 不链接 Update。
- FS 关闭时 FileLog 不拉入 LittleFS。
- 关闭 Bus/Fs/Health 不产生静态对象。
- 外部最小应用仅 `#include <Esp32Base.h>` 并启用 FULL profile 时，不额外声明 framework 内置库也必须编译通过。

## 6. OTA 检查

必须通过：

- 未认证不能写 flash。
- 未调用 `setAuth()` 时 OTA route 不注册。
- SHA256 正确路径成功。
- SHA256 错误路径失败。
- `Update.end(true)` 只在 SHA256 通过后调用。
- Watchdog remove/restore。
- Config deferred flush pause/resume。
- WiFi power save off/restore。
- 中途断电不变砖。
- rollback 可用。
- 上传页进度显示正确。
- 进度字节数包含 raw bytes 和 KB/MB。

## 7. 配网检查

必须通过：

- 无凭证进入 AP config portal。
- 有凭证但连接失败时不自动进入 AP。
- Web 在 config portal 下启动。
- DNS 拦截生效。
- 所有 DNS 查询通配到 AP IP。
- iOS / Android / macOS / Windows 可进入配网页。
- WiFi 配置页拒绝空 SSID / 空密码。
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
- `clearLibraryNamespaces()` 不误删业务 namespace。

## 9. 文件日志检查

必须通过：

- FS profile 默认启用 WARN 文件日志。
- 非 FS profile 不强行启用 FileLog。
- 默认路径为 `/logs/eb_app.log`。
- 默认轮转为 `4 × 32KB`。
- INFO/DEBUG 仅在 file level 降低后写文件。
- INFO/DEBUG 使用 `1KB / 2s` 缓存。
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
- `readBytesAt()` 支持分页读取，文件不存在和 offset 越界返回失败，EOF 短读返回实际长度。
- `writeBytesAt()` 支持已有文件固定位置覆盖，文件不存在和写越界返回失败，不隐式扩展文件。
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
- WiFi 配置 API。
- Logs 页面。
- Logs clear POST + confirm + once + 303。
- FS/FileLog 不可用时 Logs 页面显示 unavailable。
- 日志内容 HTML escape。
- 默认 Web 首页和导航开箱可用。
- 业务优先导航可设置 device name、home path、home mode、system nav mode。
- `addPage()` / `addNavItem()` 注册的业务入口进入业务导航且不重复业务首页入口。
- 内置 Home/WiFi/OTA/Logs/Reboot 标签可覆盖，用于本地化。
- 重启按钮二次确认。
- 自定义路由 begin 前注册。
- 自定义路由 Web ready 后注册。
- JSON escape。
- 大字节数 raw bytes + KB/MB 展示。
- 慢请求日志。
- 长 handler 限制已文档化。

## 13. Examples 检查

必须通过：

- `examples/basic` 继续覆盖 profile/芯片/Core 版本矩阵。
- `examples/basic` 的 `deps_*.cpp` 哨兵继续验证裁剪。
- `examples/full_demo` 可在自身目录 `pio run`。
- `examples/web_logs_ota` 可在自身目录 `pio run`。
- `examples/net_runtime` 可在自身目录 `pio run`。
- 所有启用 FS 的示例将文件日志设为 INFO。

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
