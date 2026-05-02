# Esp32Base 整体设计任务书

这是用户原始设计任务书的根目录占位文件，后续不会由实现过程自动迁移或删除。

正式维护中的设计文档放在 `docs/00_design.md`，并使用中文编号文档格式。

核心约束保持不变：

- 新项目只支持 ESP32 / ESP32-S3 / ESP32-C3。
- 不支持 ESP8266。
- 不兼容旧 `EspBase` API，不提供迁移层。
- 公开模块收敛为 `Esp32Base`、`Esp32BaseConfig`、`Esp32BaseNetwork`、`Esp32BaseWeb`、`Esp32BaseRuntime`。
- 所有正式文档放到 `docs/` 目录下并使用编号文件名。
- 所有样例程序都是 PlatformIO 项目格式。
