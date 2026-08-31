# 示例烧录命令

以下命令均从Esp32Base仓库根目录执行。烧录会写设备，执行前先确认串口和目标板。

通用格式：

```sh
pio run -d examples/<sample> -e <env> -t upload
pio device monitor -d examples/<sample> -e <env>
```

指定串口：

```sh
pio run -d examples/<sample> -e <env> -t upload --upload-port /dev/cu.usbserial-XXXX
```

## app_events_demo

```sh
pio run -d examples/app_events_demo -e esp32_local -t upload
pio run -d examples/app_events_demo -e esp32s3_local -t upload
```

## full_demo

```sh
pio run -d examples/full_demo -e esp32_local -t upload
pio run -d examples/full_demo -e esp32s3_local -t upload
```

## web_logs_ota

```sh
pio run -d examples/web_logs_ota -e esp32_local -t upload
pio run -d examples/web_logs_ota -e esp32s3_local -t upload
```

## net_runtime

该示例从OFFLINE Profile显式增加WiFi/DNS/NTP/mDNS，但不启用Web/OTA。

```sh
pio run -d examples/net_runtime -e esp32_offline_net -t upload
pio run -d examples/net_runtime -e esp32s3_offline_net -t upload
```

## web_ui_gallery

```sh
pio run -d examples/web_ui_gallery -e esp32_local_ui_gallery -t upload
pio run -d examples/web_ui_gallery -e esp32s3_local_ui_gallery -t upload
```

## basic

ESP32 / Arduino Core 2.x：

```sh
pio run -d examples/basic -e esp32_minimal -t upload
pio run -d examples/basic -e esp32_offline -t upload
pio run -d examples/basic -e esp32_local -t upload
pio run -d examples/basic -e esp32_iot -t upload
```

ESP32-S3：

```sh
pio run -d examples/basic -e esp32s3_minimal -t upload
pio run -d examples/basic -e esp32s3_offline -t upload
pio run -d examples/basic -e esp32s3_local -t upload
pio run -d examples/basic -e esp32s3_iot -t upload
```

ESP32-C3：

```sh
pio run -d examples/basic -e esp32c3_minimal -t upload
pio run -d examples/basic -e esp32c3_offline -t upload
pio run -d examples/basic -e esp32c3_local -t upload
pio run -d examples/basic -e esp32c3_iot -t upload
```

ESP32 / Arduino Core 3.x：

```sh
python3 scripts/ensure_arduino3_platformio.py
python3 scripts/pio_arduino3.py run -d examples/basic -e esp32_minimal_arduino3 -t upload
python3 scripts/pio_arduino3.py run -d examples/basic -e esp32_offline_arduino3 -t upload
python3 scripts/pio_arduino3.py run -d examples/basic -e esp32_local_arduino3 -t upload
python3 scripts/pio_arduino3.py run -d examples/basic -e esp32_iot_arduino3 -t upload
```
