# Arduino Core 兼容性

## 1. 支持范围

支持：

- Arduino ESP32 Core 2.0.14+
- Arduino ESP32 Core 3.0.4+

芯片：

- ESP32
- ESP32-S3
- ESP32-C3

## 2. 条件编译标识

必须使用：

```cpp
ESP_ARDUINO_VERSION_MAJOR
ESP_ARDUINO_VERSION_MINOR
CONFIG_IDF_TARGET_ESP32
CONFIG_IDF_TARGET_ESP32S3
CONFIG_IDF_TARGET_ESP32C3
```

不得依赖未检查的版本行为。

## 3. Watchdog 差异

Arduino Core 2.x：

```cpp
esp_task_wdt_init(timeoutSeconds, true);
```

Arduino Core 3.x：

```cpp
esp_task_wdt_config_t config = {};
esp_task_wdt_init(&config);
```

实现必须用版本宏隔离。

## 4. WiFi event 差异

STA 连接前会在 `WiFi.mode(WIFI_STA)` 后、`WiFi.begin()` 前调用 `WiFi.setHostname(Esp32Base::hostname())`，确保 Core 2.x / 3.x 下 DHCP client hostname 使用当前 Esp32Base hostname。

Core 2.x 使用：

- `SYSTEM_EVENT_*`

Core 3.x 使用：

- `ARDUINO_EVENT_*`

WiFi event callback 中只允许：

- 更新轻量状态。
- 入固定队列。

不允许：

- publish Bus。
- 写 NVS。
- 启动 Web。
- 做重操作。

## 5. Sleep 差异

ESP32 / ESP32-S3：

- RTC GPIO wake 能力较完整。

ESP32-C3：

- wake source 受限。
- 不提供不可实现的统一承诺。

不支持的 wake source：

- 返回 false。
- 输出 warn。

## 6. mDNS 差异

mDNS 在 Core 3.x / IDF v5 下频繁 destroy/recreate 风险较高。

当前策略：

- 断网时软停止：标记 not advertising。
- 重连后刷新 service record。
- 避免频繁 destroy/recreate mDNS 底层对象。
- `stop()` 对外语义是停止广告，不承诺释放底层 mDNS 全部资源。

## 7. Update 差异

`Update.begin/write/end` 基本一致，但：

- Core 3.x 分区校验更严格。
- LOCAL和IOT Profile必须在Core 2.x / 3.x都验证OTA。
- SHA256 使用 mbedTLS streaming API 边接收边更新，不把完整固件载入 RAM。
- 只有 SHA256 校验通过后才能调用 `Update.end(true)`。
- Core 2.x / 3.x 都必须验证错误 SHA256 不切换分区。

## 8. PSRAM

当前版本不依赖 PSRAM。

不使用 PSRAM 作为默认资源预算前提。

## 9. ESP32-C3 特殊约束

ESP32-C3：

- 单核。
- 无 PSRAM。
- wake source 限制更多。
- 部分模块默认容量更保守；Web route 默认仍统一为 24，应用可按自身 route 数量显式调小。

必须验证：

- WiFi + Web + Fs 同时运行。
- Watchdog 不误触发。
- OTA 可完成。
- Captive Portal 可用。

## 10. ESP32-S3 USB-CDC

ESP32-S3 开发板常见 USB-CDC 串口配置差异。

要求：

- 日志初始化不能假设传统 UART 一定可见。
- 示例文档应提示常见 `USB CDC On Boot` 设置。
- 发布前必须实测 Serial 日志在 ESP32-S3 Arduino Core 2.x / 3.x 下可观察。

## 11. Brownout 控制

默认不关闭 brownout detector。

当 `ESP32BASE_OTA_DISABLE_BROWNOUT_DURING_WRITE=1` 时：

- 仅 OTA flash write 窗口内临时关闭。
- 成功、失败、abort、client disconnect 全路径恢复。
- 实现必须用版本和芯片条件编译隔离寄存器差异。
- 不支持的 core/chip 组合下必须编译失败或降级为 warn，不允许静默假装成功。

启用该宏前，需要在 Core 2.x / 3.x 分别确认 brownout register 名称和 restore 方式；未确认的组合必须保持默认关闭。

## 12. OTA rollback weak hook

Arduino ESP32 core 在 `CONFIG_APP_ROLLBACK_ENABLE` 启用时会声明 weak `verifyOta()` 和 `verifyRollbackLater()`。当前本地 core 2.x 源码中，默认 `verifyRollbackLater()` 返回 `false`，默认 `verifyOta()` 返回 `true`；因此 `initArduino()` 会在 running partition 处于 `pending_verify` 时很早调用 `esp_ota_mark_app_valid_cancel_rollback()`。

Esp32Base 的策略：

- `ESP32BASE_OTA_REQUIRE_MARK_VALID=0` 时不覆盖该 weak hook，保持既有 Arduino 行为。
- `ESP32BASE_OTA_REQUIRE_MARK_VALID=1` 时定义 `extern "C" bool verifyRollbackLater()`，阻止 Arduino core 在业务初始化前自动 mark valid，并在该 hook 内为 `pending_verify` 镜像启动一次性 ESP-IDF 系统计时器。
- 业务必须在应用自检通过后调用 `Esp32BaseOta::markCurrentValid()`。
- setup 阶段崩溃/复位由 bootloader rollback；未进入 `Esp32Base::begin()`、setup 阻塞或未调用 `handle()` 时由独立确认计时器触发 rollback；`handle()` timeout 只作为计时器建立失败时的降级路径。

## 13. PlatformIO / pioarduino 验证注意事项

`examples/basic` 同时提供官方 PlatformIO Arduino Core 2.x 环境和 pioarduino Arduino Core 3.x 环境。

官方 `platformio/espressif32@6.7.0` 与 pioarduino 当前都会使用 `framework-arduinoespressif32` 这个包名。两类环境交替构建时，PlatformIO 可能会卸载并重新安装对应 framework。`examples/basic` 的 Core 3.x env 显式固定 pioarduino 的 Arduino 3.3.8 framework 主包和 libs 包，并通过 `scripts/pioarduino_core3_preflight.py` 在构建前检查关键包路径。做 Core 3.x 验证前仍建议先运行预热脚本，避免构建脚本在主 framework 包缺失或半安装状态下拿到空的 `FRAMEWORK_DIR`：

```sh
python3 scripts/ensure_arduino3_platformio.py
```

如果该脚本失败，先按本地 PlatformIO 包管理状态处理；不要把 `FRAMEWORK_DIR` 为空、framework 主包缺失或工具包反复安装直接判断为源码兼容失败。

推荐验证顺序：

```sh
platformio run -d examples/basic -e esp32_minimal -e esp32_local -e esp32_iot -j 1
python3 scripts/ensure_arduino3_platformio.py
platformio run -d examples/basic -e esp32_local_arduino3 -e esp32_iot_arduino3 -j 1
platformio run -d examples/basic -e esp32_minimal_arduino3 -j 1
```

外部应用通过 `lib_deps = file:///.../Esp32Base` 引用本库时，LOCAL/IOT及显式FS/Web/OTA能力必须能由本库自行触发PlatformIO LDF发现Arduino ESP32内置库，不要求业务代码额外include `LittleFS.h`、`WiFi.h`、`WebServer.h`或`Update.h`。

本库的可选实现文件使用直接framework include给LDF提供依赖线索。PlatformIO可能会为MINIMAL外部应用构建一些未链接的framework archive；裁剪验收以最终ELF/map是否链接WiFi/WebServer/Update/LittleFS符号为准。

如需同时验证 Core 2.x 与 Core 3.x，记录首次失败时应区分：

- 编译、链接、符号或 API 错误：按源码兼容问题处理。
- framework 包缺失、`FRAMEWORK_DIR` 为 `None`、工具包反复安装：先运行 `scripts/ensure_arduino3_platformio.py`，安装完成后重跑确认。
