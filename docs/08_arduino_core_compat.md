# Arduino Core 兼容性

## 1. 支持范围

源码兼容目标：

- Arduino ESP32 Core 2.0.14+
- Arduino ESP32 Core 3.0.4+

当前发布构建矩阵实际固定验证 Arduino Core 2.0.16 和 3.3.8。范围内其它 minor 版本属于兼容目标，不等于已经逐版本验证；产品固定其它版本时必须重新执行对应芯片、Profile、OTA 和资源检查。

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

## 11. Brownout

Esp32Base 不修改 brownout detector 寄存器，不提供 OTA 期间关闭欠压保护的宏，因此没有 Core 2.x / 3.x 寄存器兼容分支。供电跌落诊断只读取 reset reason；实际 OTA 掉电安全依赖芯片复位、bootloader 和双 OTA 分区，并必须在目标硬件验证。

## 12. OTA rollback weak hook

Arduino ESP32 core 在 `CONFIG_APP_ROLLBACK_ENABLE` 启用时会声明 weak `verifyOta()` 和 `verifyRollbackLater()`。当前本地 core 2.x 源码中，默认 `verifyRollbackLater()` 返回 `false`，默认 `verifyOta()` 返回 `true`；因此 `initArduino()` 会在 running partition 处于 `pending_verify` 时很早调用 `esp_ota_mark_app_valid_cancel_rollback()`。

Esp32Base 的策略：

- `ESP32BASE_OTA_REQUIRE_MARK_VALID=0` 时不覆盖该 weak hook，保持既有 Arduino 行为。
- `ESP32BASE_OTA_REQUIRE_MARK_VALID=1` 时定义 `extern "C" bool verifyRollbackLater()`，阻止 Arduino core 在业务初始化前自动 mark valid，并在该 hook 内为 `pending_verify` 镜像启动一次性 ESP-IDF 系统计时器。
- 业务必须在应用自检通过后调用 `Esp32BaseOta::markCurrentValid()`。
- setup 阶段崩溃/复位由 bootloader rollback；未进入 `Esp32Base::begin()`、setup 阻塞或未调用 `handle()` 时由独立确认计时器触发 rollback；`handle()` timeout 只作为计时器建立失败时的降级路径。

## 13. PlatformIO / pioarduino 验证注意事项

`examples/basic` 同时提供官方 PlatformIO Arduino Core 2.x 环境和 pioarduino Arduino Core 3.x 环境。

官方 `platformio/espressif32@6.7.0` 与 pioarduino 都使用 `framework-arduinoespressif32` 包名。如果共用默认 `~/.platformio`，Core 2.x / 3.x 交替解析时可能互相替换 framework，形成包清单显示3.x但目录部分来自2.x的非原子状态。该问题属于本机包管理冲突，不是源码兼容错误。

仓库将两代framework都做物理隔离，发布验证不使用默认`~/.platformio`：

- `.piohome/arduino2`只保存`platformio/espressif32@6.7.0`、Arduino Core 2.0.16及对应工具链；
- `.piohome/arduino3`只保存固定发布`pioarduino 55.03.38-1`、Arduino Core 3.3.8、framework libs及对应工具链；
- `scripts/ensure_arduino_platformio.py`安装并核对两边的固定版本和关键路径；
- `scripts/pio_arduino.py 2 ...`与`scripts/pio_arduino.py 3 ...`分别设置对应`PLATFORMIO_CORE_DIR`，同时把每个工程的build/libdeps输出隔离到`.pio/.../arduino2`或`.pio/.../arduino3`，并拒绝明显选错代际的env；
- Core 3.x env的`scripts/pioarduino_core3_preflight.py`在真正编译前再次检查。

推荐验证顺序：

```sh
python3 scripts/ensure_arduino_platformio.py
python3 scripts/pio_arduino.py 2 run -d examples/basic \
  -e esp32_minimal -e esp32_offline -e esp32_local -e esp32_iot -j 1
python3 scripts/pio_arduino.py 3 run -d examples/basic \
  -e esp32_minimal_arduino3 -e esp32_offline_arduino3 \
  -e esp32_local_arduino3 -e esp32_iot_arduino3 -j 1
```

这种双目录方案会多占一份平台和工具链磁盘空间，但换来双向隔离和可复现验证；任何一代都不会受用户默认目录或其它项目替换同名framework包影响。预热或wrapper失败时先按输出修复对应隔离目录；不要把包目录不完整直接判断为源码兼容失败。

外部应用通过 `lib_deps = file:///.../Esp32Base` 或 `symlink://.../Esp32Base` 引用本库时，必须在同一 `lib_deps` 中显式列出所选Profile使用的Arduino内置库。PlatformIO默认 `chain` LDF不会可靠递归扫描Esp32Base私有 `.cpp/.inc` 实现；不能依赖偶然的全局包状态，也不能让业务源码添加 `LittleFS.h`、`WiFi.h`、`WebServer.h`或`Update.h` 等无业务语义的占位include。LOCAL/IOT的完整清单以 `docs/13_integration_and_upgrade.md` 模板为准，MINIMAL/OFFLINE按 `examples/basic/platformio.ini` 裁减；Core 3.x必须额外声明拆分出的内置`Networking`和`Hash`。

本库`library.json`中的framework include flags只解决Esp32Base私有实现编译时的头文件可见性，不替代应用`lib_deps`对内置archive的发现和链接。声明依赖可能编译最终未使用的archive；裁剪验收仍以最终ELF/map是否链接WiFi/WebServer/Update/LittleFS符号为准。

如需同时验证 Core 2.x 与 Core 3.x，记录首次失败时应区分：

- 隔离目录完整且出现编译、链接、符号或 API 错误：按源码兼容问题处理；
- framework包缺失、`FRAMEWORK_DIR` 为 `None`、Network/Hash头文件缺失或工具包反复安装：只删除损坏的`.piohome/arduino2`或`.piohome/arduino3`，重新运行统一预热脚本，再通过对应代际wrapper重跑。
