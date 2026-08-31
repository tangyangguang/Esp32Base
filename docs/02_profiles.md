# Profile 与裁剪

## 1. 定位

Profile 只表达四种经过验证的典型设备底座，不枚举能力排列组合：

```text
MINIMAL -> OFFLINE -> LOCAL -> IOT
```

大多数具备本地维护面的产品选择 `LOCAL`；需要公网 MQTT 的智能终端选择 `IOT`。特殊设备从最接近的 Profile 出发，用明确的 `ESP32BASE_ENABLE_*` 宏覆盖能力。

默认 Profile：

```cpp
ESP32BASE_PROFILE_MINIMAL
```

当前仍处于首版定型阶段，不提供旧 Profile 名称、兼容别名或迁移层。

## 2. 最终 Profile

| Profile | 默认能力 | 典型用途 |
| --- | --- | --- |
| `MINIMAL` | Log、Config、System | 极小程序、专项测试、完全自定义组合 |
| `OFFLINE` | MINIMAL、Watchdog、Health、FS、FileLog | 无网络但需要本地可靠运行与诊断的控制器 |
| `LOCAL` | OFFLINE、WiFi、恢复配网、DNS、NTP、Time、mDNS、认证 Web、Web OTA | 可在局域网维护和升级的普通设备 |
| `IOT` | LOCAL、MQTT/MQTTS | 同时具备本地维护面和公网 MQTT 的智能终端 |

Log、Config、System是Facade无条件依赖的核心能力，不允许通过能力宏关闭。

`LOCAL` 和 `IOT` 的 OTA 均指统一的 HTTP Web OTA：浏览器使用 multipart 上传，PlatformIO `pio run -t webota` 使用高速 raw binary 上传。ArduinoOTA/espota 不属于当前库。

## 3. 正交能力

以下能力默认不随四个 Profile 启用，应用按真实硬件和产品需求显式开启：

- Bus
- Sleep
- RTC
- RS485 Port
- Record Store
- App Events及条件跟踪
- App Config

这些能力存在跨设备复用价值，但不是每台典型设备都需要。Profile 不隐藏其容量、存储格式或硬件所有权。

例如：

```ini
build_flags =
  -D ESP32BASE_PROFILE=ESP32BASE_PROFILE_LOCAL
  -D ESP32BASE_ENABLE_RTC=1
  -D ESP32BASE_RTC_DRIVER=ESP32BASE_RTC_DRIVER_DS3231
  -D ESP32BASE_ENABLE_RECORD_STORE=1
  -D ESP32BASE_ENABLE_APP_EVENTS=1
  -D ESP32BASE_ENABLE_APP_CONFIG=1
  -D ESP32BASE_APP_CONFIG_MAX_GROUPS=3
  -D ESP32BASE_APP_CONFIG_MAX_FIELDS=12
```

## 4. 展开契约

编译期配置分为两个文件：

- `Esp32BaseProfile.h`：定义四个 Profile 并展开其默认能力；
- `Esp32BaseCapabilityConfig.h`：定义未指定能力的默认值、容量和依赖检查。

处理顺序固定为：

1. 读取用户选择的 Profile；
2. 仅用 `#ifndef` 展开 Profile 默认能力；
3. 保留用户显式 `ESP32BASE_ENABLE_*` 覆盖；
4. 补齐其他能力和容量默认值；
5. 执行全部依赖和值域检查。

Profile 默认值不能覆盖用户显式 `-D`。因此非典型无 Web 联网节点可以使用：

```ini
build_flags =
  -D ESP32BASE_PROFILE=ESP32BASE_PROFILE_OFFLINE
  -D ESP32BASE_ENABLE_WIFI=1
  -D ESP32BASE_ENABLE_DNS=1
  -D ESP32BASE_ENABLE_NTP=1
  -D ESP32BASE_ENABLE_MDNS=1
```

不为这种能力排列新增第五个 Profile。

## 5. 能力依赖

硬依赖：

- `WEB` 需要 `WIFI`；
- `OTA` 需要 `WIFI + WEB`；启用即包含 OTA 核心、Web上传、分区诊断、SHA256、mark-valid与回滚；
- `DNS`、`NTP`、`MDNS` 需要 `WIFI`；
- `NTP`、`RTC`、`RECORD_STORE` 需要 `TIME`；
- `TIME` 在启用 NTP、RTC、Record Store、App Events、Web或MQTT时默认开启；
- `MQTT` 需要 `WIFI + TIME`，MQTTS运行时还要求NTP来源；
- `FILELOG` 需要 `FS`，启用FS时默认开启FileLog；
- `APP_EVENTS` 需要 `RECORD_STORE`，后者需要 `FS + TIME`；
- `APP_EVENT_CONDITIONS` 需要 `APP_EVENTS`；
- `APP_CONFIG` 需要 `WEB`；
- `RS485_PORT` 无内部硬依赖。

软依赖：

- WiFi、Web、OTA、Health不依赖Bus；
- MQTT与OTA通过顶层facade协调生命周期，不形成Network反向依赖Update；
- RTC、RS485、Sleep等独立能力不因Profile选择而自动占用硬件。

## 6. 四个 Profile 的裁剪目标

### MINIMAL

必须不链接：WiFi、DNS、mDNS、WebServer、Update、LittleFS、MQTT。

### OFFLINE

必须链接：FS、FileLog、Watchdog、Health。

必须不链接：WiFi、DNS、mDNS、WebServer、Update、MQTT。

### LOCAL

必须链接：WiFi、DNS、NTP、mDNS、WebServer、LittleFS、Update和Web OTA。

必须不链接：MQTT。Bus、Sleep、RTC、RS485、Record Store、App Events和App Config只在显式开启时进入固件。

### IOT

必须包含LOCAL全部能力及 `Esp32BaseMqtt`。MQTT只提供单Broker连接、TLS、安全默认值、固定邮箱、QoS 0/1、重连和诊断；Topic、payload、命令、幂等和业务Schema仍在应用层。

所有Profile均必须不存在ArduinoOTA、3232监听端口和espota协议符号。

## 7. 容量与分区

仓库示例默认使用推荐分区表：

- ESP32 4MB：`partitions/esp32-4mb-ota-balanced.csv`
- ESP32-C3 4MB：`partitions/esp32-c3-4mb-ota-balanced.csv`
- ESP32-S3 8MB：`partitions/esp32-s3-8mb-ota-balanced.csv`

`LOCAL`/`IOT`必须保证当前固件能放入下一OTA app slot。业务路由、静态资源、MQTT邮箱、App Config字段和业务Store都应按实际需求核算，不通过Profile假定无限容量。

## 8. 验证矩阵

`examples/basic` 是四Profile的构建与裁剪基线，覆盖ESP32、ESP32-S3、ESP32-C3，并对代表性ESP32环境覆盖Arduino Core 2.x/3.x。

至少运行：

```sh
platformio run -d examples/basic \
  -e esp32_minimal -e esp32_offline -e esp32_local -e esp32_iot
python3 scripts/check_trim_symbols.py
```

发布前再执行三芯片和Core 3.x矩阵。裁剪以最终ELF/map为准，不能只凭宏或依赖声明判断。
