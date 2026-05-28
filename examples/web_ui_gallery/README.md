# Web UI Gallery

本示例用于集中查看和验证 Esp32Base Web UI baseline，不承载具体业务逻辑。

它覆盖：

- 状态概览。
- 统计摘要。
- 带筛选和分页的记录列表。
- 紧凑配置列表。
- 一次性操作命令。
- 流程向导。
- 诊断维护。
- 访问控制状态。
- 高风险确认保护。
- 空状态。
- 多字段独立表单。
- 内置 App Config 入口和底部系统状态栏。

顶部导航只保留高频主入口；统计、流程、维护、访问控制、确认保护和空状态等页面通过父级入口进入，并使用父级路径前缀保持导航上下文。
记录页包含真实筛选控件、当前页附近页码、每页条数和跳页提交；配置页包含简单字段行内展开编辑和多字段独立编辑页。多字段表单使用字段宽度类区分短字段、中等字段和长字段，避免平均拉满。

## 构建

```bash
pio run -d examples/web_ui_gallery -e esp32_web_ui_gallery
```

ESP32-S3：

```bash
pio run -d examples/web_ui_gallery -e esp32s3_web_ui_gallery
```

## 烧录查看

```bash
pio run -d examples/web_ui_gallery -e esp32_web_ui_gallery -t upload
pio device monitor -d examples/web_ui_gallery -b 115200
```

默认 hostname 是 `esp32base-ui`。设备连网后访问：

- `http://esp32base-ui.local`
- 或串口输出中的 IP 地址

默认账号：

- 用户名：`admin`
- 密码：`admin`

## 自测固件

selftest env 会在 Web ready 后请求所有样例页面和关键 POST 跳转，串口输出 `selftest summary pass=... total=...`。

```bash
pio run -d examples/web_ui_gallery -e esp32_web_ui_gallery_selftest -t upload
pio device monitor -d examples/web_ui_gallery -b 115200
```

自测主要验证路由、认证、分页链接、结果提示和 `POST -> 303 -> GET`，视觉效果仍需要在 PC 和手机浏览器中人工查看。
