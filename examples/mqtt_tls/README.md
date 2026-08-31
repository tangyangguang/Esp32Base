# MQTT TLS Example

This example uses the `IOT` Profile, which combines the local Web/OTA maintenance plane with the MQTT 3.1.1 client.

Before flashing, copy `local_secrets.example.h` to `local_secrets.h` and replace
the broker host, credentials and CA certificate. The local file is ignored by
Git. The committed values are non-working placeholders.

The official precompiled Arduino Core 2.0.16 and 3.3.8 packages validate the CA
chain and hostname, but are built without X.509 `notBefore`/`notAfter` checks.
This example therefore contains the explicit
`ESP32BASE_MQTT_ALLOW_UNCHECKED_CERTIFICATE_DATES=1` product opt-in. Remove that
flag when using a Core built with `CONFIG_MBEDTLS_HAVE_TIME_DATE=y`. MQTTS waits
for NTP; an RTC alone does not release the TLS gate.

该示例通过 `symlink://../..` 以外部库方式接入 Esp32Base，并显式声明IOT Profile所需Arduino内置库；Core 3.x env额外声明拆分出的`Networking`和`Hash`。这同时是外部应用验证PlatformIO默认`chain` LDF契约的发布检查，不要用业务源码中的占位include代替这些依赖。

Build:

```sh
python3 scripts/ensure_arduino_platformio.py
python3 scripts/pio_arduino.py 2 run -d examples/mqtt_tls
python3 scripts/pio_arduino.py 2 run -d examples/mqtt_tls -e esp32s3_mqtt_tls
python3 scripts/pio_arduino.py 2 run -d examples/mqtt_tls -e esp32c3_mqtt_tls
python3 scripts/pio_arduino.py 3 run -d examples/mqtt_tls -e esp32_mqtt_tls_arduino3
```

The example deliberately publishes only current state after connection and
reconnection. Command deduplication, expiry, topic versioning, authorization and
business state synchronization remain application responsibilities.
