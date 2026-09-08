# 受控 TLS 工具链

## 适用范围

Arduino Core 3.3.8 的官方预编译库未开启证书有效期校验。受控产物使用原包记录的源码版本、组件锁和配置，通过 Espressif Arduino Library Builder 重新构建，启用 `CONFIG_MBEDTLS_HAVE_TIME_DATE=y`。不通过修改头文件、替换单个函数或关闭校验绕过限制。

当前完成构建与链接验证的是 **Core 3.3.8 / ESP32**。ESP32-S3、ESP32-C3 的来源已锁定，但尚未完成对应构建与验证；Core 2.x 尚未纳入此构建工具。原有 Core 2/Core 3 支持范围不因此改变，不能把这一结果外推到全部组合。

产物属于 Esp32Base 的底层构建工具，不依赖 IoT 平台或 SDK，也不涉及 Esp8266Base。应用仍使用 Arduino/PlatformIO。现有工程的工具链不会自动替换。

不使用 TLS 的 LOCAL 应用无需因此切换工具链；需要完整 TLS 校验的应用再选择经过验证的对应产物。

## 来源与改变

`scripts/tls_toolchain_core3.json` 锁定：

- 原始库版本 `5.5.4+sha.735507283d`，以及各芯片原包的配置、组件锁和版本记录哈希。
- Library Builder `14a0af99059169a14049e4f72a0785586b2f85be`。
- ESP-IDF `735507283d5b2f9fb363a1901172dbd9e847945d`。
- 构建原包使用的 Arduino 源码 `c6428be92c5eccb279511adf88c60f27cb2c338d` 和 TinyUSB 源码 `eae25b531dff32636aecb21ca4a8a3ac8e391fc3`。

固件配置只允许日期校验这一项变化。网络缓冲、任务栈、重连及其他配置发生变化时，审计失败；组件锁变化也直接失败。源码只允许工具明确生成的配置补充和下面的构建依赖调整。

ESP32 的 DIO 配套导出器实际只使用 `spi_flash` 库和生成的配置头，因此其构建依赖从整个应用 ELF 收窄到 `__idf_spi_flash` 及上游依赖，避免重复生成整套应用。导出列表、导出命令和固件源码保持上游内容；导出列表变化时工具拒绝继续。其他芯片保持上游构建依赖。

开启日期校验也会启用上游 RainMaker 的对应时间同步等待条件。Esp32Base 不使用 RainMaker；直接使用该上游组件的应用须考虑此行为。MQTTS 的可信时间、CA、hostname 校验及失败诊断仍不可省略，NTP 就绪与证书校验能力是两个不同条件。

## 构建与验证

在仓库根目录执行。构建器面向 macOS/Linux；当前验证环境为 macOS arm64、Python 3.14。需要 Git、CMake、Ninja、jq；macOS 还需要 GNU sed、GNU awk 和 GNU realpath。macOS 的 ccache 使用锁定哈希的官方独立包；Linux 需在 PATH 提供 ccache。

```bash
python3 scripts/ensure_arduino_platformio.py
python3 scripts/build_tls_toolchain.py prepare --target esp32
python3 scripts/build_tls_toolchain.py build --target esp32
python3 scripts/build_tls_toolchain.py package --target esp32
python3 scripts/test_tls_toolchain.py
```

`prepare` 下载固定源码和 ESP-IDF 工具；源码、Python 环境及编译缓存位于 `.cache/tls-toolchain/`。首次构建成本较高，复用同一版本产物即可，不要求业务项目自行复建。工具不拉取最新分支，不覆盖未知源码改动，同一工作目录不允许并发构建。上游构建器会清理专属目录中的 `build/`、`out/` 等生成物，不要把用户文件放在那里。

主机侧证书行为验证使用隔离 ESP-IDF Python 环境中的 `cryptography`，例如当前已验证环境：

```bash
.cache/tls-toolchain/idf-tools/python_env/idf5.5_py3.14_env/bin/python scripts/test_tls_certificates.py
python3 scripts/test_tls_platformio.py
```

前者验证同版上游 X.509 源码的正常、过期、尚未生效、错误 hostname、不受信任签发者五种情况，并检查具体错误标志。后者只使用公开示例占位配置，核对包的来源及文件哈希，要求 SDK 头文件实际开启日期校验，并检查最终 ELF 中的证书链验证和日期路径。两者均不连接设备或 Broker。

PlatformIO 验证通过 `scripts/pio_arduino.py 3 --tls-toolchain ...` 使用 `.piohome/arduino3-tls`，不替换 `.piohome/arduino2`、`.piohome/arduino3`。这个参数只选择隔离目录，不会自动安装或选择受控库；实际库来源仍由工程的 `platform_packages` 声明。常规构建继续使用原有命令。

## 本地产物

`package` 先检查配置、组件锁、证书链验证器对日期路径的实际引用，以及原包全部内存变体的配置头，再生成 `.cache/tls-toolchain/package-esp32/`。包只含 ESP32，不能供 S3/C3 使用。

`esp32base-build.json` 保存来源锁、构建脚本哈希和产物文件哈希。已存在的包不会被覆盖；再次生成前应明确保留或移走旧包。可单独执行 `audit` 检查已生成的上游输出，无需重跑构建。哈希记录用于识别具体产物及内容变化，不等于发布签名。

本地验证使用如下依赖，Arduino Core 与 PlatformIO 平台版本仍保持工程既定的 3.3.8 配置：

```ini
platform_packages =
  framework-arduinoespressif32 @ https://github.com/espressif/arduino-esp32/releases/download/3.3.8/esp32-core-3.3.8.tar.xz
  framework-arduinoespressif32-libs @ file:///absolute/path/to/package-esp32
```

缓存目录不是正式分发地址。正式接入多个业务项目前，应保存已验证的固定产物及哈希，再决定发布位置；不能依赖每个项目从上游最新源码临时构建。

## 当前验证结果与边界

Core 3.3.8 / ESP32 的完整配置对比仅有 `CONFIG_MBEDTLS_HAVE_TIME_DATE: n -> y`，组件锁不变；QIO/DIO 两套配置头及编译后的日期路径通过审计。五个主机证书用例通过；使用该产物的公开 MQTT 示例在隔离 PlatformIO 环境编译、链接成功，最终 ELF 保留证书链验证、UTC 转换和日期比较函数。

该示例当前构建静态占用见 [统一验证结果](15_validation_results.md)，不在此重复维护体积数字。这不是与原版的资源差值。另有 ESP32 实验板的隔离 TLS 证书、MQTT/Web 并发与 OTA 验证，详见同一结果文档；不能将这些特定负载的短时结果外推为任意业务缓冲或长期稳定性保证。

尚未验证：设备上的正常/过期/未生效证书握手、错误时钟和恢复、断网重连、Web/OTA 并发资源边界及长时间稳定性。没有烧录、OTA、操作真实设备或发布工具链产物。S3、C3 和 Core 2.x 仍需独立闭环。

上游资料：[Library Builder](https://docs.espressif.com/projects/arduino-esp32/en/latest/lib_builder.html)、[ESP-IDF mbedTLS 配置源码](https://github.com/espressif/esp-idf/blob/735507283d5b2f9fb363a1901172dbd9e847945d/components/mbedtls/Kconfig)。

MQTT 示例不再默认开启 `ESP32BASE_MQTT_ALLOW_UNCHECKED_CERTIFICATE_DATES`。使用官方缺少日期验证的预编译 SDK 时，示例可编译但 TLS 配置会明确拒绝；实际安全握手需要上文受控包。不要把编译成功等同于 TLS 可用，也不要为跑通示例关闭有效期校验。

## 本机固定交付位置

已将验证过的 ESP32 / Core 3.3.8 产物复制到独立于构建缓存的 `local_private/toolchains/esp32-core-3.3.8-tls-6da95a99041ac119/`。目录后缀是构建凭据文件 SHA256 的前16位；复制前后逐项核对凭据中的全部文件哈希、源码锁和构建脚本哈希，未重建 SDK。应用可将上文 `file://` 路径指向此固定目录。该目录不纳入源码 Git 或基础库发布包；清理构建缓存时不得连带清理它。

这是本机可复用的固定产物，不是远端发布地址；迁移机器时连同 `esp32base-build.json` 完整保存并重新核对哈希。团队或公开分发仍需确定保存位置与发布授权。
