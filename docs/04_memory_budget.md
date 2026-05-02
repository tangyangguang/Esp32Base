# 内存与容量预算

## 1. 目标

第一版目标是不依赖 PSRAM，在普通 ESP32 DevKit 上完成 WiFi、Web、OTA、NTP、mDNS、LittleFS 和 Watchdog 的基础能力。

目标 free heap：

| 场景 | ESP32 DevKit 目标 |
| --- | --- |
| 仅启动核心模块 | >= 250KB |
| WiFi 已连接，Web 已启动 | >= 210KB |
| Web 页面/API 活跃 | >= 180KB |
| OTA 上传中最低值 | >= 120KB |
| LittleFS 小文件操作中 | >= 170KB |
| 全功能示例启动后 | >= 170KB |

这些是实机运行目标，不等同于 PlatformIO 静态 RAM 占用。

## 2. 编译体积观察

当前所有样例都会链接基础库完整能力，因此即使 basic 样例也会包含 WiFi、WebServer、Update 和 LittleFS。

最近一次编译观察中，4MB ESP32 / ESP32-C3 目标 Flash 占用大约在 67% 到 69% 区间，仍能放入 0x140000 OTA app 分区。

## 3. 固定容量

默认固定容量：

```text
ESP32BASE_CONFIG_PENDING_MAX = 8
ESP32BASE_WEB_MAX_APP_ROUTES = 16
ESP32BASE_FILE_READ_MAX = 4096
ESP32BASE_HOSTNAME_LEN = 32
```

## 4. 实现约束

- 栈上临时缓冲默认不超过 1KB。
- Web 状态 JSON 使用固定 char buffer。
- 应用路由表固定容量。
- 文件读取便捷 API 有大小上限。
- 第一版不使用 STL 容器保存常驻核心结构。

## 5. 分区建议

建议使用 `partitions/` 下的 OTA balanced 分区：

- `esp32-4mb-ota-balanced.csv`
- `esp32-s3-8mb-ota-balanced.csv`
- `esp32-c3-4mb-ota-balanced.csv`

4MB 分区为 OTA 留出两个 0x140000 app slot，并保留 LittleFS 和 coredump。
