# Esp32Base

`Esp32Base` 是面向 ESP32 / ESP32-S3 / ESP32-C3、基于 Arduino Core 的应用基础库。

这是一个全新项目，只参考旧 `EspBase` 的经验，不保持旧 API 兼容，也不支持 ESP8266。

入口文档：

- [设计说明](docs/00_design.md)
- [架构说明](docs/01_architecture.md)
- [API 说明](docs/02_api.md)
- [Web 扩展说明](docs/03_web_extension.md)
- [内存与容量预算](docs/04_memory_budget.md)
- [诊断与实机压测](docs/05_diagnostics.md)
- [basic PlatformIO 样例](examples/basic)

样例：

- [basic](examples/basic)
- [wifi_network](examples/wifi_network)
- [wifi_web_ota](examples/wifi_web_ota)
- [runtime_tools](examples/runtime_tools)
- [custom_web](examples/custom_web)
- [sleep_watchdog](examples/sleep_watchdog)

当前已实现：

- `Esp32Base`
- `Esp32BaseConfig`
- `Esp32BaseRuntime`
- `Esp32BaseNetwork`
- `Esp32BaseWeb`
- 内部日志宏
- 启动诊断
- NVS get/set 和 deferred 写入
- heap、flash、reset、wake 诊断
- WiFi STA、无凭证 AP 配置模式、NTP、mDNS
- WebServer、Basic Auth、状态 API、WiFi 设置接口、Web OTA
- 应用自定义 Web 页面/API 注册
- Watchdog、deep sleep、LittleFS 基础文件读写
