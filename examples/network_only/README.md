# Network Only

非典型联网节点样例：从 `OFFLINE` Profile 出发，用显式 `ESP32BASE_ENABLE_*` 覆盖宏增加 WiFi、DNS、NTP 和 mDNS，但**不**启用 Web 和 OTA，因此也没有本地维护面。

它对应 [Profile 与裁剪](../../docs/02_profiles.md) 中“能力覆盖不新增第五个 Profile”的约定，验证该组合可以独立构建运行：

```ini
-D ESP32BASE_PROFILE=ESP32BASE_PROFILE_OFFLINE
-D ESP32BASE_ENABLE_WIFI=1
-D ESP32BASE_ENABLE_DNS=1
-D ESP32BASE_ENABLE_NTP=1
-D ESP32BASE_ENABLE_MDNS=1
```

固件本身只调用 `Esp32Base::begin()`/`handle()`，联网与时间状态通过日志观察；需要本地维护面的产品应直接使用 `LOCAL` Profile，而不是此组合。

```sh
python3 scripts/pio_arduino.py 2 run -d examples/network_only            # ESP32
python3 scripts/pio_arduino.py 2 run -d examples/network_only -e esp32s3
python3 scripts/pio_arduino.py 2 run -d examples/network_only -e esp32c3
```
