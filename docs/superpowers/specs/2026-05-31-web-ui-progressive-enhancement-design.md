# Web UI Progressive Enhancement Design

日期：2026-05-31

## 背景

当前 Web UI baseline 已经提供统一页面壳层、紧凑信息行、表单、分页、结果提示和轻量按钮样式。现有表单主路径仍以 `POST -> 303 -> GET` 为主，稳定、简单、可防刷新重复提交，但在业务页中会带来两个体验问题：

- 单字段编辑或简单小表单需要跳转或整页刷新，滚动到页面中部时会丢失当前位置。
- 提交后页面闪烁，行内编辑框消失/重建过程不够流畅，设备控制台观感不成熟。

目标是在不把 ESP32 Web 做成重型 SPA 的前提下，把常见轻量交互沉淀到基础库，让业务库默认获得更顺滑的体验，同时保留无 JavaScript 回退和服务端安全校验。

同一轮实现还包含根路径首页策略：业务模式默认优先使用 `/index` 作为业务首页，裸 `/` 跳转到 `/index`；如果应用显式 `setHomePath("/")` 并注册 GET `/` 业务页，则裸 `/` 直接调用业务 handler。`/esp32base` 的系统入口行为保持不变。

## 目标

- 基础库托管常见轻量交互，业务页尽量不手写 JavaScript。
- 单字段行内编辑默认局部展开、局部提交、局部替换，不整页刷新。
- 1-3 个字段的小表单默认在当前页弹出层中完成，提交成功后关闭弹层并局部更新。
- 保留现有 HTML fallback：无 JS 或 fetch 不可用时仍可使用普通链接、普通表单和 `POST -> 303 -> GET`。
- AJAX 只改变交互方式，不降低认证、同源、参数、权限和设备状态校验要求。
- 默认视觉继续保持清爽、低密度、成熟设备控制台风格，避免厚重弹窗和强刺激配色。
- `HOME_APP` / `HOME_COMBINED` 支持 `/index` 默认业务首页和显式根路径业务 handler，避免 `setHomePath("/")` 自跳转。

## 非目标

- 不引入前端路由、全局状态管理或完整 SPA 运行时。
- 不把所有表单都自动改成局部提交。
- 不默认局部处理重启、格式化、清日志、OTA、文件上传、Auth 修改等高风险或长任务操作。
- 不要求业务接口只提供 JSON；基础库必须允许 HTML fallback 继续工作。
- 不在第一批重做 App Config 的完整提交确认流程。

## 交互范围

第一批支持两类交互。

### 单字段行内编辑

适用：

- 一个业务字段的轻量修改，例如名称、阈值、开关、短枚举。
- 用户需要保留上下文和滚动位置的配置列表。
- 成功结果只需要更新当前行或当前卡片。

行为：

- 点击编辑后，在当前行下方或行内展开编辑区。
- 编辑区包含标题、输入控件、错误提示区域、取消和保存按钮。
- 保存中禁用提交按钮，显示轻量状态，不改变页面滚动。
- 成功后只替换目标片段，编辑区消失，当前值更新。
- 失败后编辑区保留，输入值不丢失，错误显示在编辑区内。
- 取消后收起编辑区，不发请求。

### 小表单弹出层

适用：

- 1-3 个字段的轻量编辑或一次性参数确认。
- 不值得跳转新页面，但字段数超过单字段行内编辑。
- 成功后只需更新某个行、卡片或页面级提示。

默认形态：

- 桌面端使用居中小弹窗，宽度克制，背景轻遮罩。
- 手机端自动变为贴底或近全宽面板，保证输入和按钮可点。
- 弹层不使用大面积主色、强阴影或复杂动画；动画只允许短暂淡入/位移。

行为：

- `Esc`、取消按钮、点击遮罩可关闭。
- 如果表单已被修改，关闭前提示确认放弃更改。
- 保存中禁用提交按钮，错误显示在弹层内。
- 成功后关闭弹层，局部更新触发它的目标片段，并可显示轻量 notice。
- 失败后弹层不关闭，保留用户输入。

## 仍保持整页或明确流程的操作

以下操作不默认使用基础库局部刷新 helper：

- 重启设备。
- 格式化 LittleFS。
- 清日志。
- OTA 上传。
- 文件上传、下载、删除管理流程。
- Web Auth 修改。
- 需要长时间进度、连接可能中断、或后果不可逆的业务操作。

这些操作仍应使用明确确认、现有上传进度、结果页或 `POST -> 303 -> GET`，避免用户误以为页面局部成功就代表设备状态已经稳定。

## API 设计

新增能力应以 helper 方式暴露，保持业务代码接近现有写法。

建议 public helper：

```cpp
Esp32BaseWeb::sendInfoRowInlineEdit(...);
Esp32BaseWeb::sendInfoRowDialogForm(...);
Esp32BaseWeb::sendInlineAjaxResult(...);
Esp32BaseWeb::sendAjaxError(...);
```

具体签名在实现计划中细化，但语义固定：

- `sendInfoRowInlineEdit()` 输出当前值、编辑入口、fallback 编辑 URL 或 POST action、目标片段 id。
- `sendInfoRowDialogForm()` 输出打开弹层的按钮和弹层表单定义，字段数量控制在 1-3 个。
- `sendInlineAjaxResult()` 返回局部替换 HTML、可选 notice 和可选 focus/close 指令。
- `sendAjaxError()` 返回统一错误 JSON，前端显示在当前编辑区或弹层内。

业务如果需要完全自定义 HTML，可继续使用 `sendChunk()`，但推荐复用基础库约定的 class、data attribute 和结果格式。

## AJAX 请求约定

启用渐进增强的表单仍然是普通 HTML 表单：

- `method="post"`。
- `action` 指向现有或新增 POST endpoint。
- 表单带基础库约定的 `data-eb-ajax` 标记。
- 表单必须能在无 JS 时正常提交。

AJAX 请求增加头：

- `X-Esp32Base-Ajax: 1`
- `Accept: application/json`

服务端根据请求头决定返回 JSON 还是现有 redirect。这样同一个 endpoint 可以同时服务 AJAX 和 fallback。

成功响应：

```json
{
  "ok": true,
  "target": "row-channel-name",
  "html": "<div class='urow' id='row-channel-name'>...</div>",
  "notice": {
    "tone": "ok",
    "title": "Saved",
    "message": "Channel name updated."
  },
  "close": true
}
```

失败响应：

```json
{
  "ok": false,
  "error": "Name must be 1-12 characters."
}
```

约束：

- `html` 由服务端生成，并继续使用基础库 HTML escape。
- 客户端不拼接来自用户输入的 HTML。
- 如果响应不是 JSON、状态码不是 2xx、或 `ok=false`，前端都视为失败并保持当前编辑状态。

## 前端运行时

基础库在默认 head 中加入一段小型 JS，职责限制为：

- 发现并接管 `data-eb-ajax` 表单。
- 发现并处理 `data-eb-dialog` 弹层入口。
- 发现并处理 `data-eb-inline-edit` 行内展开入口。
- 使用 `fetch` 提交 URL encoded 表单。
- 管理按钮禁用、轻量 loading、错误显示、目标片段替换和弹层关闭。
- 在不支持 fetch 或发生不可恢复异常时放弃接管，让浏览器走原始表单行为。

运行时不负责：

- 业务字段校验规则。
- 业务状态判断。
- 复杂客户端状态同步。
- 批量 DOM diff。

## 安全与一致性

- AJAX POST 必须继续走现有认证和同源检查。
- 服务端必须重新校验所有参数，不能信任前端校验。
- 危险操作不得仅依赖弹层或按钮颜色表达风险。
- 局部更新后的 HTML 必须由服务端生成，确保和整页渲染一致。
- 同一业务操作的 fallback redirect 结果和 AJAX 局部结果应表达同一个状态。
- 过期页面或状态冲突时返回错误，提示用户刷新或重新打开编辑区。

## 第一批落地范围

第一批实现聚焦证明基础能力：

- 根路径业务首页：`HOME_ESP32BASE` 继续 `/ -> /esp32base`；业务模式默认 `/ -> /index`；显式 `setHomePath("/")` 时 `/` 直接渲染业务 handler，`/esp32base` 仍可访问系统融合首页。
- 在 `examples/web_ui_gallery` 中把“第 1 路名称”改为单字段行内编辑，展示局部展开、局部保存、局部替换。
- 在 `examples/web_ui_gallery` 中新增一个 2-3 字段小表单弹层示例，展示保存成功后关闭弹层并更新目标行或卡片。
- 增加基础 CSS：行内编辑区、弹层、遮罩、弹层错误、保存中状态。
- 增加基础 JS：AJAX submit、片段替换、弹层打开/关闭、dirty close confirm。
- 增加静态检查和示例 selftest，覆盖 helper 输出、JS/CSS 是否存在、AJAX endpoint 是否同时支持 JSON 和 fallback。

第二批再评估内置页接入：

- System Hostname 可作为单字段或小表单弹层候选。
- Footer bar 和 File log 模式可作为小表单弹层候选。
- App Config 暂不整体改为 AJAX，保留完整确认流程。

## 验证策略

- 静态脚本检查基础 CSS、JS、helper 标记、JSON 结果函数和文档标记。
- `examples/web_ui_gallery` selftest 验证：
  - 无 AJAX header 时 POST 仍返回 303。
  - 带 AJAX header 时 POST 返回 JSON。
  - JSON 成功结果包含目标片段 HTML。
  - 参数错误返回 `ok:false` 和错误文案。
- PlatformIO 构建：
  - `examples/web_ui_gallery -e esp32_web_ui_gallery`
  - `examples/full_demo -e esp32_full`
- 浏览器视觉检查：
  - 桌面端弹层居中且不厚重。
  - 手机端弹层转为贴底/近全宽。
  - 行内编辑展开后不造成按钮溢出或文字重叠。
  - 保存失败不清空输入。

## 文档更新

实现时同步更新：

- `README.md`：说明基础库支持轻量局部交互和 fallback 边界。
- `docs/03_api.md`：新增 helper、请求头、JSON 结果约定。
- `docs/04_web.md`：更新 Web 页面行为规范、哪些操作可局部刷新、哪些必须整页确认。
- `docs/11_web_ui_baseline.md`：更新视觉和交互 baseline。
- `CHANGELOG.md`：记录业务接入方式、推荐使用场景和风险边界。

## 设计结论

采用“基础库托管常见场景的渐进增强”方案。单字段编辑默认行内局部更新，小表单默认弹出层局部提交，高风险和长任务继续保留明确整页流程。这个方案能解决闪烁、滚动位置丢失和简单表单跳转割裂的问题，同时保持 ESP32 基础库轻量、安全、可回退。
