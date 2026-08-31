# 诊断与测试

## 1. 验证策略

验证分为三层：

- 自动化构建和符号裁剪，确认 profile 边界。
- Native harness，覆盖无需实机即可稳定验证的配置、Web helper 和时间源行为。
- 实机验收，确认 WiFi、Web、OTA、Sleep、Watchdog、Captive Portal 和 Flash/FS 运行时行为。

发布前的完整清单见 [发布检查清单](09_release_checklist.md)。本页只描述诊断目标和测试分层，不维护重复的逐项发布矩阵。

## 2. 自动化验证

Profile 裁剪以最终构建产物为准，不只看 `library.json` 或源码条件编译：

- MINIMAL不链接WiFi / WebServer / Update / LittleFS / MQTT。
- NET 不链接 WebServer / Update / LittleFS，除非显式启用 FS。
- WEB 不链接 Update。
- FS 关闭时 FileLog 不拉入 LittleFS。
- RTC 芯片驱动按构建目标二选一，不运行时共存。

推荐命令：

```bash
pio test -e native_config_harness
pio test -e native_web_harness
pio test -e native_time_harness
pio test -e native_time_pcf8563_harness
pio run -d examples/basic -e esp32_minimal -e esp32_offline -e esp32_local -e esp32_iot
python3 scripts/check_trim_symbols.py

# 可选：读取 Git 忽略的本机配置，对真实 MQTTS Broker 验证
# TLS/认证、错误密码、QoS 0/1、retain 和 LWT
python3 scripts/check_mqtt_cloud_integration.py
```

脚本检查应验证边界和结果。源码字符串检查只用于安全、发布包和符号裁剪这类难以用 native harness 覆盖的契约，不用于固定普通内部函数名、日志文案或实现拆分方式。

## 3. 实机验收

实机验收用于覆盖模拟环境无法证明的路径：

- OTA 成功、认证拒绝、SHA256 失败拒绝、中途断电和 rollback。
- Captive Portal 在主流系统上可进入配网页。
- WiFi 已保存凭据失败时持续重连并 backoff，不自动开放 AP；安全启动保护能跳出连续危险复位。
- LittleFS 挂载、文件读写、损坏文件处理和 Web FS 维护操作。
- Watchdog、Sleep、RTC/NTP、mDNS 和长时间运行。
- Web Auth、危险 POST、同源检查、状态/API 页面和业务路由。
- App Events、FileLog 和系统诊断日志在 FS 故障、格式化、清空和重启后的行为。

无法实机覆盖的高风险路径应在发布记录中写清缺口。

## 4. 启动诊断

启动诊断应覆盖：

- boot session / restart log。
- firmware name、version、build 和 profile。
- hostname。
- reset reason 和 wake reason。
- heap / flash。
- NVS pending。
- WiFi、FS、Web、OTA、FileLog、RTC/NTP 等已启用模块状态。

诊断日志按模块初始化顺序输出，便于定位失败模块。没有可信真实时间前，日志使用启动后毫秒数；RTC 或 NTP 建立可信时间后可切换为绝对时间。`boot_count` 表示普通重启会话计数，不等同于上电次数或每次 deep sleep 唤醒次数。

日志不得输出 WiFi 或 Web Auth 密码值。允许输出 SSID、Web Auth 用户名、来源、结果和 `password_set` 这类状态。

## 5. 运行诊断

Health 负责周期性运行状态：

- uptime。
- loop max period。
- health tick 时间。

其他诊断字段通过对应模块查询：

- heap / min heap：`Esp32BaseSystem`。
- reset reason：`Esp32BaseSystem`。
- WiFi state：`Esp32BaseWiFi`。
- FS state：`Esp32BaseFs`。
- FileLog state：`Esp32BaseFileLog`。
- MQTT：`Esp32BaseMqtt::status()` 和 `diagnostics()`，包括等待条件、证书日期校验能力、连接/重连、最近连接时间、接收/publish accepted/PUBACK、送达状态不确定、丢弃、outbox/inbox/control mailbox 高水位、in-flight 和底层错误码。密码、私钥和 payload 不进入这些结构。

默认周期：

```cpp
ESP32BASE_HEALTH_TICK_INTERVAL_MS=30000
ESP32BASE_HEALTH_DEBUG_LOG_INTERVAL_MS=1800000
```

默认系统诊断日志模式为 ERROR，降低全功能固件的 Flash 写入量；现场排查可显式切到 WARN 或 INFO。Serial 日志和 FileLog 可独立控制，编译期 `ESP32BASE_LOG_LEVEL` 仍是日志上限。

## 6. 故障诊断

故障诊断应让用户能区分“不可用、用户关闭、写入故障、认证失败、输入非法和维护动作失败”等状态，避免只给出泛化错误。

MQTT 至少区分：未配置、配置非法、等待 WiFi、等待可信时间、退避、连接中、已连接、Broker 拒绝、TLS/证书、普通 transport/DNS 和应用显式暂停。ESP-IDF/Core 版本不能稳定细分的 DNS/socket 原因统一归入 `dns_or_transport`，同时保留 native ESP/TLS/socket code，不把推断伪装成精确错误。

需要覆盖的故障类别：

- optional module begin failed。
- NVS 写入失败或写满。
- WiFi 连接失败、backoff、config portal、DNS 启动失败。
- Web Auth 失败、危险 POST 被拒绝、WiFi 表单校验失败。
- LittleFS mount/read/write/format 失败。
- FileLog append/rotate/clear/reload 失败。
- App Events append/read/header/record/clear 失败。
- OTA start/write/verify/end 失败和低频进度。
- Watchdog reset、rollback reason、sleep/restart 生命周期。
- MQTT 配置、WiFi/Time gate、Broker 拒绝、TLS、分片超限、邮箱/outbox/in-flight 满、断线后送达状态不确定和重新订阅。

故障日志应表达结果、原因和可维护动作，不应要求调用方依赖固定英文日志 marker。

## 7. 回归测试

Bug fix 应补对应回归覆盖，优先顺序：

- 可用 native harness 稳定复现的，写 native 行为测试。
- 依赖链接裁剪的，用构建和符号检查。
- 依赖真实 WiFi、Flash、OTA、Watchdog、RTC 或 Captive Portal 的，写入发布检查清单或实机记录。

测试应验证结果和边界，不强制代码必须长成某个内部实现形状。

`native_mqtt_harness` 使用 fake ESP-MQTT transport 验证配置/危险 CA 值拒绝、WiFi/NTP gate、回调只在 handle 分发、重新订阅及 SUBACK 拒绝、QoS 1 ACK/送达状态不确定、outbox 上限、分片组装、超限丢弃、认证/DNS/证书错误、终止拒绝不被 WiFi 恢复绕过、证书日期校验能力诊断和 millis 回绕。`native_mqtt_secure_default_harness` 单独证明底层未启用证书日期校验且应用未显式 opt-in 时，TLS 配置以专用错误失败。`check_mqtt_cloud_integration.py` 使用 Git 忽略的 `local_private/emqx_mqtt.ini` 与 CA 文件，从开发机验证真实 Broker 的 TLS 主机名/CA、正确和错误密码、QoS 0/1、retain 清理和 LWT；脚本不打印凭据。Broker 重启、ESP32 WiFi/modem sleep、TLS heap、task stack 和长稳仍必须使用实机验证。
