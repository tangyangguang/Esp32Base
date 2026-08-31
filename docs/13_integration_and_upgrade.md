# 应用接入与版本适配

本文给出业务项目接入当前 `Esp32Base` 的最短正确路径，以及使用旧 Profile 或旧 OTA 接口的项目一次性适配方法。当前版本只维护一套最终契约，不提供旧宏、旧 API 或旧协议兼容层。

## 1. 新项目接入顺序

1. 根据产品是否需要网络、本地维护面和 MQTT，选择一个 Profile。
2. 按硬件和业务需要显式开启 RTC、RS485、Record Store、App Events、App Config 等正交能力。
3. 选择与芯片和 Flash 容量匹配的仓库分区表。
4. 在 `Esp32Base::begin()` 前完成硬件安全态、Web 默认认证、RTC 总线和业务 Web/API 注册。
5. 检查 `Esp32Base::begin()` 返回值；正常循环持续调用 `Esp32Base::handle()`。
6. 先通过主机测试和目标板构建，再做串口启动、Web、HTTP OTA、断网和真实执行器验证。

### Profile 选择

| 产品需要 | Profile |
| --- | --- |
| 仅核心日志、配置和系统信息 | `ESP32BASE_PROFILE_MINIMAL` |
| 无网络，但需要 Watchdog、Health、LittleFS 和文件日志 | `ESP32BASE_PROFILE_OFFLINE` |
| 需要 WiFi、配网、NTP、mDNS、认证 Web 和 HTTP Web OTA | `ESP32BASE_PROFILE_LOCAL` |
| 在 LOCAL 基础上还需要 MQTT/MQTTS | `ESP32BASE_PROFILE_IOT` |

RTC、Bus、Sleep、RS485、Record Store、App Events 和 App Config 不随 Profile 自动开启。不要为了一个正交能力选择更重的 Profile，也不要创建新的组合 Profile。

## 2. PlatformIO 最小模板

以下模板适用于同级目录中的本地 `Esp32Base` 仓库；使用其它目录时只调整路径。

```ini
[env:product]
platform = platformio/espressif32@6.7.0
board = esp32dev
framework = arduino
board_build.partitions = ../../Esp32Base/partitions/esp32-4mb-ota-balanced.csv
lib_deps =
  WiFi
  DNSServer
  ESPmDNS
  LittleFS
  WebServer
  Update
  Wire
  symlink://../../Esp32Base
build_flags =
  -D ESP32BASE_PROFILE=ESP32BASE_PROFILE_LOCAL
  -D ESP32BASE_DEFAULT_HOSTNAME=\"product-device\"
```

`MINIMAL`、`OFFLINE` 和特殊裁剪组合应参考 `examples/basic/platformio.ini` 显式列出实际 Arduino 内置依赖，避免 PlatformIO LDF 因示例源码或全局包状态意外带入 WiFi/Web。ESP32-S3 与 ESP32-C3 应改用对应 board 和 `partitions/` 中对应分区表。

典型本地控制器可继续增加：

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

容量宏必须按实际注册数量核算，不能依赖多余默认槽位。

## 3. 初始化骨架

```cpp
#include <Esp32Base.h>
#include <Wire.h>

void setup() {
    // 第一项先把执行器置于安全态。
    initializeOutputsOff();

    Wire.begin();
#if ESP32BASE_ENABLE_RTC
    Esp32BaseRtc::configure(Wire);
#endif

#if ESP32BASE_ENABLE_WEB
    Esp32BaseWeb::setDefaultAuth(PRODUCT_WEB_USER, PRODUCT_WEB_PASSWORD);
    registerBusinessWebRoutes();
#endif

    Esp32Base::setFirmwareInfo("product", "1.0.0");
    if (!Esp32Base::begin()) {
        enterSafeDiagnosticState();
        return;
    }

    initializeBusinessStorageAndState();
}

void loop() {
    handleBusinessStateMachine();
    Esp32Base::handle();
}
```

业务不得假定 `begin()` 一定成功。Web handler、MQTT callback 和文件操作应保持有界、非阻塞；泵、阀、电机、门锁等执行器的安全态和业务状态机仍由应用负责。

## 4. Web 与认证

启用 Web 的正式固件必须满足以下之一：

- `begin()` 前调用 `Esp32BaseWeb::setDefaultAuth(user, password)`；
- 设备 NVS 中已经存在有效 `eb_web` 认证。

两者都不存在时 Web fail-closed。不要启用 `ESP32BASE_WEB_ALLOW_INSECURE_DEFAULT_AUTH` 作为量产默认值，也不要记录、回显或提交真实密码。

业务首页、路由、POST 同源与认证检查、静态资源、App Config 和 System 维护边界见 `docs/04_web.md`；页面样式和 helper 见 `docs/11_web_ui_baseline.md`。

## 5. HTTP Web OTA

当前只存在 HTTP Web OTA：

- 浏览器 multipart：`/esp32base/ota`；
- PlatformIO raw binary：`/esp32base/ota/raw`，通过 `pio run -t webota` 调用。

应用的 `platformio.ini`：

```ini
extra_scripts =
  post:../../Esp32Base/scripts/esp32base_webota.py
```

设备地址和 Web Auth 放在 Git 忽略的本地配置中：

```ini
custom_esp32base_webota_host = product-device.local
custom_esp32base_webota_user = <current-web-auth-user>
custom_esp32base_webota_password = <current-web-auth-password>
```

不要配置 `upload_protocol = espota`，不要依赖 UDP 发现或 3232 端口。量产回滚、自检和 `ESP32BASE_OTA_REQUIRE_MARK_VALID` 见 `docs/05_ota.md`。

## 6. 旧应用一次性适配

### 旧 Profile 对应关系

旧 Profile 不再定义。以下表格用于修改业务构建配置，不表示库中保留兼容关系。

| 旧选择 | 当前等价起点 | 为保持旧能力还需显式开启 |
| --- | --- | --- |
| `CORE` | `MINIMAL` | 无 |
| `RUNTIME` | `OFFLINE` | `ESP32BASE_ENABLE_BUS=1`、`ESP32BASE_ENABLE_SLEEP=1` |
| `NET` | `MINIMAL` | WiFi、DNS、NTP、mDNS |
| `NET_RUNTIME` | `OFFLINE` | Bus、Sleep、WiFi、DNS、NTP、mDNS |
| `WEB` | `MINIMAL` | Bus、WiFi、DNS、NTP、mDNS、Web |
| `WEB_RUNTIME` | `OFFLINE` | Bus、Sleep、WiFi、DNS、NTP、mDNS、Web |
| `FULL` | `LOCAL` | 如果业务使用过 Bus 或 Sleep，分别显式开启 |

不要只机械替换名称：先检查应用是否实际使用 Bus、Sleep、Web、OTA、MQTT、RTC 和存储，再保留真实需要的能力。需要 MQTT 的最终产品直接选择 `IOT`，不要使用 `LOCAL + MQTT` 作为常规配置。

### 必须删除或替换的旧契约

| 旧内容 | 当前处理 |
| --- | --- |
| `ESP32BASE_ENABLE_WEB_OTA` | 删除；`ESP32BASE_ENABLE_OTA` 统一控制 HTTP Web OTA |
| `ESP32BASE_ENABLE_ARDUINO_OTA` | 删除 |
| `ArduinoOTA` include、依赖、回调和密码同步 | 删除 |
| `upload_protocol = espota`、端口3232配置 | 改用 `esp32base_webota.py` 和 Web Auth |
| `Esp32BaseOta::beginNetworkServices()` | 删除调用；网络服务由 `Esp32Base::begin()` 管理 |
| `Esp32BaseWeb::authUser()` / `authPassword()` | 删除调用；明文认证不再提供读取 API |

编译器会对旧 Profile、旧 OTA 宏、非法能力依赖和关闭核心能力给出明确错误。不要在业务仓库重新定义同名宏或增加兼容头文件绕过这些错误。

### 配置与数据

本次 Profile/HTTP OTA 定型没有改变 `Esp32BaseConfig`、WiFi、Web Auth、hostname、App Config、LittleFS 或 Record Store 的现有存储含义。只要业务自身配置结构和 key 未改变，重新编译、串口烧录或 OTA 后应继续保留已有有效配置，默认值不得覆盖用户值。

如果业务同时修改了自己的 blob、文件或 Record Store 结构，则按业务当前规则处理；基础库不会猜测或迁移不透明业务数据。结构不兼容时应保持执行器安全关闭，并明确提示重新配置或执行经确认的维护动作。

## 7. 适配验收清单

现有项目修改后至少确认：

- 全仓不存在旧 Profile、旧 OTA 宏、`ArduinoOTA`、`espota` 和 3232 监听配置；
- Profile 默认能力与所有显式 `ESP32BASE_ENABLE_*` 相符；
- 正交硬件能力没有因 Profile 收敛而意外丢失；
- Web 默认认证在 `begin()` 前设置，正式构建不存在不安全默认认证；
- `Esp32Base::begin()` 失败时业务保持安全停机；
- `Esp32Base::handle()` 在正常循环持续执行；
- 分区表、最终 `firmware.bin` 和下一 OTA slot 余量满足产品门槛；
- 原有有效 NVS/LittleFS 配置在不清存储的升级后仍生效；
- 串口启动日志、WiFi/配网、Web、HTTP OTA、断网运行和恢复符合预期；
- IOT 项目额外验证 MQTTS 时间门禁、CA、重连、QoS、LWT、heap 峰值和长稳。

基础库自身的完整发布矩阵见 `docs/09_release_checklist.md`。业务项目必须在自己的 README 中记录实际命令、资源结果和尚未完成的实机验证，不能把基础库构建成功等同于产品验收完成。
