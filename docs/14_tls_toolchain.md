# 受控 TLS 工具链

## 适用范围

Arduino Core 3.3.8 的官方预编译库未开启证书有效期校验。受控产物使用原包记录的源码版本、组件锁和配置，通过 Espressif Arduino Library Builder 重新构建，启用 `CONFIG_MBEDTLS_HAVE_TIME_DATE=y`，并仅在 ESP32 产物中将 MQTT task 固定到 Core 0、收敛为 Base 实际使用的 TLS 客户端及 MQTT TCP/TLS 传输。不通过修改头文件、替换单个函数或关闭校验绕过限制。

当前完成构建与链接验证的是 **Core 3.3.8 / ESP32**。ESP32-S3、ESP32-C3 的来源已锁定，但尚未完成对应构建与验证；Core 2.x 尚未纳入此构建工具。原有 Core 2/Core 3 支持范围不因此改变，不能把这一结果外推到全部组合。

产物属于 Esp32Base 的底层构建工具，不依赖 IoT 平台或 SDK，也不涉及 Esp8266Base。应用仍使用 Arduino/PlatformIO。现有工程的工具链不会自动替换。

不使用 TLS 的 LOCAL 应用无需因此切换工具链；需要完整 TLS 校验的应用再选择经过验证的对应产物。

## 来源与改变

`scripts/tls_toolchain_core3.json` 锁定：

- 原始库版本 `5.5.4+sha.735507283d`，以及各芯片原包的配置、组件锁和版本记录哈希。
- Library Builder `14a0af99059169a14049e4f72a0785586b2f85be`。
- ESP-IDF `735507283d5b2f9fb363a1901172dbd9e847945d`。
- 构建原包使用的 Arduino 源码 `c6428be92c5eccb279511adf88c60f27cb2c338d` 和 TinyUSB 源码 `eae25b531dff32636aecb21ca4a8a3ac8e391fc3`。

配置变化由来源锁明确列出：所有目标开启日期校验；ESP32 另外固定 MQTT task 在 Core 0，选择 `CONFIG_MBEDTLS_TLS_CLIENT_ONLY=y`，关闭 TLS 服务端及其 session tickets、原生 HTTPS 服务端，以及 MQTT WebSocket/WSS。Base 提供的出站 TCP/TLS、客户端双向认证和本地 HTTP Web/OTA 均保留；没有 TLS 服务端或 WebSocket 的 Base 公开接口。直接调用上游 HTTPS-server/WebSocket API 的其他应用不能使用此受控包来代替完整上游包。S3/C3 暂不添加这些目标配置。

没有调整算法、CA/hostname/date 验证、TLS 协议、客户端 session tickets、网络缓冲、任务栈、优先级或重连；未列出的有效配置变化和组件锁漂移会使审计失败。Kconfig 关闭父功能时，部分从属布尔项会从 `not set` 变为省略；审计把两者同视为 false，任何启用或参数值变化仍精确比较。源码只允许工具明确生成的配置补充和下面的构建依赖调整。

ESP32 的此项配置作用于使用该 SDK 的所有原生 MQTT task，保留优先级5与基础库默认栈预算；Arduino 默认 loop 位于 Core1，从而避免 TLS 握手因继承高优先级连续占用业务所在核。它不搬移调用方的 loop/system task：应用自建服务任务需要自行选择合适的核，不应再把服务任务集中到Core0后仍期待相同隔离效果。SDK锁等待、Web尾延迟和TLS/Web并发内存峰值仍存在，不提供硬实时保证；Core0上的其他计算负载需纳入产品验收。LOCAL不会因这一构建配置启动MQTT，基础库也未新增后台任务、任务名称拦截或平台依赖。单核C3没有另一个核可以隔离，不能外推该收益。

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

前者验证同版上游 X.509 源码的正常、过期、尚未生效、错误 hostname、不受信任签发者五种情况，并检查具体错误标志。后者只使用公开示例占位配置，核对包的来源及文件哈希，要求 SDK 头文件实际开启日期校验及 ESP32 MQTT Core0配置，并检查最终 ELF 中的证书链验证、日期路径，以及 WebSocket/TLS 服务端握手符号已消失。两者均不连接设备或 Broker。

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

当前产物已通过完整配置差异、组件锁、QIO/DIO 配置头和二进制审计：TLS 客户端握手及证书链日期路径存在，TLS 服务端握手不存在，MQTT 库不再引用 WebSocket。10 项审计定向测试通过，覆盖误关客户端 TLS、意外保留 WebSocket、无关内存/核/日期/依赖变化及从属项省略。公开 IOT 示例目标构建和最终 ELF 检查通过；灌溉目标完成链接，但仍因其容量门禁被拒绝，数值见[统一验证结果](15_validation_results.md)。

本轮没有改动 X.509 源码或客户端校验设置，复用已有五类同版主机证书用例证据；本轮二进制检查不能替代新产物实机握手。保留上游编译警告和旧 Kconfig 提示，没有为消除提示修改上游代码。

早期日期校验产物已经在实验板完成有效/过期/尚未生效/错误名称/不受信任证书握手、显式恢复、WiFi恢复及OTA维护验证；这些历史结果不能直接替代新产物验证，也不是任意网络、错误时钟或长期负载的全面验收。S3、C3 和 Core 2.x 的完整日期校验产物仍需独立闭环；工具链未外部发布。

上游资料：[Library Builder](https://docs.espressif.com/projects/arduino-esp32/en/latest/lib_builder.html)、[ESP-IDF mbedTLS 配置源码](https://github.com/espressif/esp-idf/blob/735507283d5b2f9fb363a1901172dbd9e847945d/components/mbedtls/Kconfig)。

MQTT 示例不再默认开启 `ESP32BASE_MQTT_ALLOW_UNCHECKED_CERTIFICATE_DATES`。使用官方缺少日期验证的预编译 SDK 时，示例可编译但 TLS 配置会明确拒绝；实际安全握手需要上文受控包。不要把编译成功等同于 TLS 可用，也不要为跑通示例关闭有效期校验。

## 本机固定交付位置

当前客户端资源优化产物固定在 `local_private/toolchains/esp32-core-3.3.8-tls-a33c13b24f5db543/`。构建凭据 `esp32base-build.json` 的 SHA256 为 `a33c13b24f5db5430246ddd5272c1385f537f71a0da6d0694d140591884d38b7`；复制前后核对来源锁、构建脚本哈希及全部 3,795 个文件哈希。应用可将 `file://` 路径指向该固定目录。目录不纳入源码 Git 或基础库发布包，清理缓存不得连带清理固定产物。

之前的 `9033b58a578a5b78` 产物已有日期校验和 MQTT core0，但没有本次角色/传输裁剪；更早 `6da95a99041ac119` 只有日期校验。旧固定产物仅用于复现对应历史提交，不代表当前推荐依赖，也不构成运行期兼容分支。

这是本机可复用的固定产物，不是远端发布地址；迁移机器时连同 `esp32base-build.json` 完整保存并重新核对哈希。团队或公开分发仍需确定保存位置与发布授权。
