# MQTT TLS Example

This example enables the optional MQTT 3.1.1 client on `NET_RUNTIME`.

Before flashing, copy `local_secrets.example.h` to `local_secrets.h` and replace
the broker host, credentials and CA certificate. The local file is ignored by
Git. The committed values are non-working placeholders.

Build:

```sh
pio run -d examples/mqtt_tls
pio run -d examples/mqtt_tls -e esp32s3_mqtt_tls
pio run -d examples/mqtt_tls -e esp32c3_mqtt_tls
pio run -d examples/mqtt_tls -e esp32_mqtt_tls_arduino3
```

The example deliberately publishes only current state after connection and
reconnection. Command deduplication, expiry, topic versioning, authorization and
business state synchronization remain application responsibilities.
