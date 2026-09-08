# MQTT TLS Example

This example uses the `IOT` Profile, which combines the local Web/OTA maintenance plane with the MQTT 3.1.1 client.

Before flashing, copy `local_secrets.example.h` to `local_secrets.h` and replace
the broker host, credentials and CA certificate. The local file is ignored by
Git. The committed values are non-working placeholders.

The official precompiled Arduino Core 2.0.16 and 3.3.8 packages validate the CA
chain and hostname, but are built without X.509 `notBefore`/`notAfter` checks.
The example does **not** enable
`ESP32BASE_MQTT_ALLOW_UNCHECKED_CERTIFICATE_DATES`. With those official libraries,
compilation may succeed but TLS configuration is rejected. For complete TLS,
use the verified ESP32 / Core 3.3.8 artifact and exact dependency settings in
[the controlled TLS toolchain guide](../../docs/14_tls_toolchain.md).
Do not bypass certificate dates to make this example connect. MQTTS waits for
NTP; an RTC alone does not release the TLS gate.

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

## 主用 ESP32 的集中实测

本节是可重复的验收步骤，不表示已经完成实机验证。先确定具体测试板、Flash/分区、当前固件来源、USB串口或网络地址和独立测试Broker；确认设备不承担生产控制。烧录、OTA、重启及制造故障均需针对该设备单独授权。

使用已验证的固定 Core 3.3.8 TLS 产物，不为测量重建 SDK。真实 MQTT 配置只放本地忽略文件，不输出或提交。`IOT` 开启 Web 能力不等于认证已配置：需要已有有效 Web Auth，或应用在 `begin()` 前从私有配置调用 `Esp32BaseWeb::setDefaultAuth()`；没有认证时Web不会启动，不得临时开启不安全默认认证。确认实际WiFi配置，避免把首次配网状态误作网络失败。

| 顺序 | 场景 | 观察与通过条件 |
| --- | --- | --- |
| 1 | 同一固定固件冷启动、NTP就绪、正常TLS连接 | 日期校验能力启用、连接和订阅成功；记录启动与稳态heap、连接次数、原生错误；不得仅以编译或配置成功判断连接成功 |
| 2 | 握手/重连期间连续访问已认证的 `/esp32base/status` | 同时记录客户端请求时延及设备heap/free、min、max alloc、loop低水位；不能把访问页面造成的开销排除后声称真实并发峰值 |
| 3 | 独立测试Broker不可达后恢复；再测试设备所用测试AP断开后恢复 | 分别记录错误类别、恢复时长、重连和订阅结果；不修改全屋路由器或生产Broker，不把Broker失败认作radio卡态 |
| 4 | 专用测试端点的过期、未生效证书，以及错误hostname/CA | 必须拒绝连接且可诊断；恢复正确条件后按当前错误恢复契约操作。主机证书用例不能替代此板端握手 |
| 5 | 经授权的正常OTA及中止上传 | 记录更新开始前后heap及恢复情况、存储检查点失败诊断；确认失败后本地服务恢复，不要求检查点失败阻断恢复OTA |
| 6 | 固定负载重复连接/页面访问，随后持续运行 | 比较同一稳态阶段的free heap、最大连续块、错误和重连趋势；不得以一次成功证明长稳，记录实际持续时间与循环次数 |

`minFreeHeap` 是自启动以来的最低值，不能直接当某一次握手的独立峰值；页面显示的 `Loop stack low-water` 也不是 MQTT task 栈。当前公开 MQTT Diagnostics 提供队列水位与错误计数，没有MQTT任务栈句柄。若需要该栈实测，应在专用测量固件的SDK任务上下文采集，先核对版本与单位，不猜任务名，不把loop数据冒充MQTT数据。采样会引入开销，报告需标注采样间隔和入口。

不预设未经产品负载确认的通用heap“安全数字”。通过条件首先包括无OOM/崩溃/意外复位、保护门禁正确、恢复路径可用、同等负载下没有持续资源流失；延迟和余量按目标产品实际负载确定。需要比较优化收益时，同一硬件、凭据、网络与负载使用可比基线，记录Base提交、Core/产物凭据、固件哈希和配置。测试完成后只把脱敏结果更新到验证文档，不保存过程性日志到源码发布包。
