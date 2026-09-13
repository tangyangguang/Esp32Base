# 示例烧录命令

以下命令均从 Esp32Base 仓库根目录执行。首次使用先运行 `python3 scripts/ensure_arduino_platformio.py`。烧录会写设备，执行前先确认串口和目标板。

通用格式（Core 2.x）：

```sh
python3 scripts/pio_arduino.py 2 run -d examples/<sample> -e <env> -t upload
python3 scripts/pio_arduino.py 2 device monitor -d examples/<sample> -e <env>
```

指定串口：

```sh
python3 scripts/pio_arduino.py 2 run -d examples/<sample> -e <env> -t upload --upload-port /dev/cu.usbserial-XXXX
```

能力样例的 env 统一为芯片名：`esp32`（默认）、`esp32s3`、`esp32c3`；RTC 样例按驱动选择 `esp32_ds3231` / `esp32_pcf8563`；MQTT 另有代表性 Core 3 env `esp32_arduino3`。

## profile_baseline

四个 Profile 的构建与裁剪基线，env 名为 `<chip>_<profile>`。

ESP32 / Arduino Core 2.x：

```sh
python3 scripts/pio_arduino.py 2 run -d examples/profile_baseline -e esp32_minimal -t upload
python3 scripts/pio_arduino.py 2 run -d examples/profile_baseline -e esp32_offline -t upload
python3 scripts/pio_arduino.py 2 run -d examples/profile_baseline -e esp32_local -t upload
python3 scripts/pio_arduino.py 2 run -d examples/profile_baseline -e esp32_iot -t upload
```

ESP32-S3：

```sh
python3 scripts/pio_arduino.py 2 run -d examples/profile_baseline -e esp32s3_minimal -t upload
python3 scripts/pio_arduino.py 2 run -d examples/profile_baseline -e esp32s3_offline -t upload
python3 scripts/pio_arduino.py 2 run -d examples/profile_baseline -e esp32s3_local -t upload
python3 scripts/pio_arduino.py 2 run -d examples/profile_baseline -e esp32s3_iot -t upload
```

ESP32-C3：

```sh
python3 scripts/pio_arduino.py 2 run -d examples/profile_baseline -e esp32c3_minimal -t upload
python3 scripts/pio_arduino.py 2 run -d examples/profile_baseline -e esp32c3_offline -t upload
python3 scripts/pio_arduino.py 2 run -d examples/profile_baseline -e esp32c3_local -t upload
python3 scripts/pio_arduino.py 2 run -d examples/profile_baseline -e esp32c3_iot -t upload
```

ESP32 / Arduino Core 3.x：

```sh
python3 scripts/pio_arduino.py 3 run -d examples/profile_baseline -e esp32_minimal_arduino3 -t upload
python3 scripts/pio_arduino.py 3 run -d examples/profile_baseline -e esp32_offline_arduino3 -t upload
python3 scripts/pio_arduino.py 3 run -d examples/profile_baseline -e esp32_local_arduino3 -t upload
python3 scripts/pio_arduino.py 3 run -d examples/profile_baseline -e esp32_iot_arduino3 -t upload
```

## local_web_app

LOCAL Profile 的业务 Web 应用范式：业务页面/API、App Config 全字段、CSV、Sleep 入口。

```sh
python3 scripts/pio_arduino.py 2 run -d examples/local_web_app -e esp32 -t upload
python3 scripts/pio_arduino.py 2 run -d examples/local_web_app -e esp32s3 -t upload
python3 scripts/pio_arduino.py 2 run -d examples/local_web_app -e esp32c3 -t upload
```

## mqtt_client

IOT Profile 的 MQTT/MQTTS 客户端。烧录前先按其 README 准备 `local_secrets.h`。

```sh
python3 scripts/pio_arduino.py 2 run -d examples/mqtt_client -e esp32 -t upload
python3 scripts/pio_arduino.py 2 run -d examples/mqtt_client -e esp32s3 -t upload
python3 scripts/pio_arduino.py 2 run -d examples/mqtt_client -e esp32c3 -t upload
python3 scripts/pio_arduino.py 3 run -d examples/mqtt_client -e esp32_arduino3 -t upload
```

## network_only

从 OFFLINE Profile 显式增加 WiFi/DNS/NTP/mDNS，但不启用 Web/OTA。

```sh
python3 scripts/pio_arduino.py 2 run -d examples/network_only -e esp32 -t upload
python3 scripts/pio_arduino.py 2 run -d examples/network_only -e esp32s3 -t upload
python3 scripts/pio_arduino.py 2 run -d examples/network_only -e esp32c3 -t upload
```

## web_ui_gallery

LOCAL Profile 的 Web UI baseline 视觉验收页。

```sh
python3 scripts/pio_arduino.py 2 run -d examples/web_ui_gallery -e esp32 -t upload
python3 scripts/pio_arduino.py 2 run -d examples/web_ui_gallery -e esp32s3 -t upload
python3 scripts/pio_arduino.py 2 run -d examples/web_ui_gallery -e esp32c3 -t upload
```

## record_store

MINIMAL Profile 加 FS/Record Store/Conditions，演示定长载荷记录。

```sh
python3 scripts/pio_arduino.py 2 run -d examples/record_store -e esp32 -t upload
python3 scripts/pio_arduino.py 2 run -d examples/record_store -e esp32s3 -t upload
python3 scripts/pio_arduino.py 2 run -d examples/record_store -e esp32c3 -t upload
python3 scripts/pio_arduino.py 3 run -d examples/record_store -e esp32_arduino3 -t upload
```

## rtc

MINIMAL Profile 加外部 RTC，构建期二选一驱动。

```sh
python3 scripts/pio_arduino.py 2 run -d examples/rtc -e esp32_ds3231 -t upload
python3 scripts/pio_arduino.py 2 run -d examples/rtc -e esp32_pcf8563 -t upload
```

## rs485

MINIMAL Profile 加半双工 RS485 串口（不是 Modbus）。默认引脚面向 classic ESP32，其他板型先改 `src/main.cpp` 引脚。

```sh
python3 scripts/pio_arduino.py 2 run -d examples/rs485 -e esp32 -t upload
```
