# RTC time source example

This example shows the board-level configuration for an external RTC.

Build one firmware for one chip. DS3231 and PCF8563 are mutually exclusive choices and are not auto-detected at runtime.

```sh
pio run -d examples/rtc_time_source -e esp32_ds3231
pio run -d examples/rtc_time_source -e esp32_pcf8563
```

Default I2C addresses are used when `ESP32BASE_RTC_I2C_ADDR=0`:

- DS3231: `0x68`
- PCF8563: `0x51`

The example calls `Wire.begin()` in the application before `Esp32Base::begin()`. If a project wants Esp32Base to call `Wire.begin()` for the RTC bus, set `ESP32BASE_RTC_AUTO_WIRE_BEGIN=1` and optionally set `ESP32BASE_RTC_SDA` / `ESP32BASE_RTC_SCL`.

Application code should normally read time through `Esp32BaseTime::snapshot()`. Use `Esp32BaseRtc` only for RTC chip status, explicit read/write, custom I2C bus binding, or maintenance logic.
