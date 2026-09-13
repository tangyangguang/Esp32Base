# Local Web App

`LOCAL` Profile 下业务 Web 应用的参考写法。它演示产品页面如何与基础库维护面共存：

- 业务页面与 API：Dashboard、Control、CSV 导出、POST→303→GET 模式；
- App Config 全部字段类型（string/int/decimal/bool/enum）、字段级与页面级校验、变更/保存回调、重启提示；
- 自定义首页模式、导航和内置页重命名、`head` 注入；
- 正交能力 Sleep 的入口（深睡 10 秒按钮）；
- OTA 自检确认（`ESP32BASE_OTA_REQUIRE_MARK_VALID=1`）。

它不是“最全能力集合”：Record Store、RTC、RS485、MQTT 等正交能力各有独立样例。

默认 env：

```sh
python3 scripts/pio_arduino.py 2 run -d examples/local_web_app            # ESP32
python3 scripts/pio_arduino.py 2 run -d examples/local_web_app -e esp32s3
python3 scripts/pio_arduino.py 2 run -d examples/local_web_app -e esp32c3
```

默认 Web Auth 为示例用的 `admin/admin`，业务项目必须改为自己的凭据来源。可选地通过环境变量注入 WiFi 凭据宏 `ESP32BASE_LOCAL_WEB_APP_WIFI_SSID` / `ESP32BASE_LOCAL_WEB_APP_WIFI_PASS`（经 `ESP32BASE_LOCAL_WEB_APP_EXTRA_FLAGS`）。烧录命令见 [../FLASHING.md](../FLASHING.md)。
