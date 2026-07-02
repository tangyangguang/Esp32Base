# RS485 port example

This example shows the lightweight `Esp32BaseRs485Port` helper for half-duplex RS485 direction control.

It is not a Modbus example. It does not implement frame parsing, CRC, register maps, retries, addressing, or any application protocol. Application code should build those pieces on top of `writeBytes()`, `readable()`, and `readByte()` when needed.

Default pins:

- RX: GPIO 16
- TX: GPIO 17
- DE: GPIO 4

The example uses `Serial2` at `9600 8N1`, sends a small text heartbeat every 5 seconds, and prints received bytes to the USB serial monitor.

```sh
pio run -d examples/rs485_port
```
