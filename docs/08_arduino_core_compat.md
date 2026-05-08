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

第一版固定策略：

- 断网时软停止：标记 not advertising。
- 重连后刷新 service record。
- 避免频繁 destroy/recreate mDNS 底层对象。
- `stop()` 对外语义是停止广告，不承诺释放底层 mDNS 全部资源。

## 7. Update 差异

`Update.begin/write/end` 基本一致，但：

- Core 3.x 分区校验更严格。
- FULL profile 必须在 Core 2.x / 3.x 都验证 OTA。
- SHA256 使用 mbedTLS streaming API 边接收边更新，不把完整固件载入 RAM。
- 只有 SHA256 校验通过后才能调用 `Update.end(true)`。
- Core 2.x / 3.x 都必须验证错误 SHA256 不切换分区。

## 8. PSRAM

第一版不依赖 PSRAM。

不使用 PSRAM 作为默认资源预算前提。

## 9. ESP32-C3 特殊约束

ESP32-C3：

- 单核。
- 无 PSRAM。
- wake source 限制更多。
- 默认容量更保守。

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

## 12. PlatformIO / pioarduino 验证注意事项

`examples/basic` 同时提供官方 PlatformIO Arduino Core 2.x 环境和 pioarduino Arduino Core 3.x 环境。

官方 `platformio/espressif32@6.7.0` 与 pioarduino 当前都会使用 `framework-arduinoespressif32` 这个包名。两类环境交替构建时，PlatformIO 可能会卸载并重新安装对应 framework：

- 从 Core 2.x 环境切到 Core 3.x 环境时，第一次 Core 3.x 构建可能先失败于 `FRAMEWORK_DIR` 为空。
- 随后任一 pioarduino 环境完成 framework 安装后，Core 3.x 环境重跑应通过。
- 这个现象属于本地 PlatformIO 包管理状态，不应直接判断为源码兼容失败。

推荐验证顺序：

```sh
platformio run -d examples/basic -e esp32_core -e esp32_full -j 1
platformio run -d examples/basic -e esp32_full_arduino3 -j 1
platformio run -d examples/basic -e esp32_core_arduino3 -j 1
```

外部应用通过 `lib_deps = file:///.../Esp32Base` 引用本库时，FULL / FS / Web / OTA profile 必须能由本库自行触发 PlatformIO LDF 发现 Arduino ESP32 内置库，不要求业务代码额外 include `LittleFS.h`、`WiFi.h`、`WebServer.h`、`Update.h` 或 `ArduinoOTA.h`。

本库的可选实现文件使用直接 framework include 给 LDF 提供依赖线索。PlatformIO 可能会为 CORE 外部应用构建一些未链接的 framework archive；裁剪验收以最终 ELF/map 是否链接 WiFi/WebServer/Update/LittleFS 符号为准。

如需同时验证 Core 2.x 与 Core 3.x，记录首次失败时应区分：

- 编译、链接、符号或 API 错误：按源码兼容问题处理。
- framework 包缺失、`FRAMEWORK_DIR` 为 `None`、工具包反复安装：按本地构建环境问题处理，安装完成后重跑确认。
