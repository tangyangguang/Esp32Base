# 示例烧录命令

以下命令均从 Esp32Base 仓库根目录执行，便于直接复制。

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
pio run -d examples/app_events_demo -e esp32_full -t upload
pio run -d examples/app_events_demo -e esp32s3_full -t upload
```

## full_demo

```sh
pio run -d examples/full_demo -e esp32_full -t upload
pio run -d examples/full_demo -e esp32s3_full -t upload
pio run -d examples/full_demo -e esp32_full_selftest -t upload
```

## web_logs_ota

```sh
pio run -d examples/web_logs_ota -e esp32_full -t upload
pio run -d examples/web_logs_ota -e esp32s3_full -t upload
```

## net_runtime

```sh
pio run -d examples/net_runtime -e esp32_net_runtime -t upload
pio run -d examples/net_runtime -e esp32s3_net_runtime -t upload
```

## web_ui_gallery

```sh
pio run -d examples/web_ui_gallery -e esp32_web_ui_gallery -t upload
pio run -d examples/web_ui_gallery -e esp32s3_web_ui_gallery -t upload
pio run -d examples/web_ui_gallery -e esp32_web_ui_gallery_selftest -t upload
```

## basic

ESP32 / Arduino Core 2.x：

```sh
pio run -d examples/basic -e esp32_core -t upload
pio run -d examples/basic -e esp32_runtime -t upload
pio run -d examples/basic -e esp32_net -t upload
pio run -d examples/basic -e esp32_net_runtime -t upload
pio run -d examples/basic -e esp32_web -t upload
pio run -d examples/basic -e esp32_web_runtime -t upload
pio run -d examples/basic -e esp32_full -t upload
```

ESP32-S3：

```sh
pio run -d examples/basic -e esp32s3_core -t upload
pio run -d examples/basic -e esp32s3_full -t upload
```

ESP32-C3：

```sh
pio run -d examples/basic -e esp32c3_core -t upload
pio run -d examples/basic -e esp32c3_full -t upload
```

ESP32 / Arduino Core 3.x：

```sh
pio run -d examples/basic -e esp32_core_arduino3 -t upload
pio run -d examples/basic -e esp32_full_arduino3 -t upload
```
