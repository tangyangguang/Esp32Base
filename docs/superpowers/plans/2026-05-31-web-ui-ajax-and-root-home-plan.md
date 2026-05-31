# Web UI Ajax And Root Home Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Esp32Base Web progressive-enhancement helpers for inline edit/dialog small forms, and add formal root-home routing so business pages can own `/` or default to `/index` without breaking `/esp32base`.

**Architecture:** Keep the Web server HTML-first: every enhanced form remains a normal form with `POST -> 303 -> GET` fallback. Add a tiny built-in JavaScript runtime that only handles marked inline/dialog forms, and add server-side helpers that emit stable fragments and JSON results. Root routing remains centralized in the built-in `/` handler, which either redirects to `/esp32base`, redirects to `/index`, or dispatches an explicitly configured root business route.

**Tech Stack:** C++/Arduino WebServer, existing Esp32Base Web helpers, static Python checks, PlatformIO example selftests.

---

## File Map

- Modify `src/web/internal/WebRouting.cpp`: add route lookup, `/index` default selection, and root direct dispatch for explicit `setHomePath("/")`.
- Modify `src/web/internal/WebInternal.h`: declare route lookup helpers used by root dispatch.
- Modify `src/web/internal/WebAssets.cpp`: add compact CSS for inline edit/dialog and a small JavaScript runtime in `WEB_HEAD`.
- Modify `src/web/Esp32BaseWeb.h` and `src/web/Esp32BaseWeb.cpp`: expose progressive-enhancement helpers and AJAX JSON helpers.
- Modify `examples/web_ui_gallery/src/main.cpp`: demonstrate `/index` default business home, inline edit, dialog small form, AJAX JSON/fallback POST coverage.
- Modify `examples/full_demo/src/main.cpp`: demonstrate explicit root business home rendering and verify `/esp32base` remains system/fusion entry.
- Create `scripts/check_web_root_home.py`: static route behavior guard.
- Extend `scripts/check_web_ui_baseline.py`: guard CSS/JS/helper markers for inline edit and dialogs.
- Update `README.md`, `docs/03_api.md`, `docs/04_web.md`, `docs/11_web_ui_baseline.md`, `CHANGELOG.md`.

---

## Task 1: RED Checks For Root Home Routing

**Files:**
- Create: `scripts/check_web_root_home.py`
- Modify: `examples/web_ui_gallery/src/main.cpp`
- Modify: `examples/full_demo/src/main.cpp`

- [ ] **Step 1: Add static failing check**

Create `scripts/check_web_root_home.py`:

```python
#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


routing = read("src/web/internal/WebRouting.cpp")
gallery = read("examples/web_ui_gallery/src/main.cpp")
full = read("examples/full_demo/src/main.cpp")
docs = read("docs/04_web.md")
changelog = read("CHANGELOG.md")

checks = [
    (routing, 'findRoute("/", Esp32BaseWeb::METHOD_GET)', "Root handler must be able to find a GET business route for '/'."),
    (routing, 'strcmp(configuredHomePath(), "/") == 0', "Root handler must special-case explicit home path '/'."),
    (routing, 'strcmp(g_routes[i].path, "/index") == 0', "Default business home must prefer a registered /index app page."),
    (routing, 'g_homeMode == Esp32BaseWeb::HOME_ESP32BASE ? "/esp32base"', "HOME_ESP32BASE must keep redirecting / to /esp32base."),
    (gallery, 'RUN_SELFTEST("GET", "/", nullptr, true, 302, "Location: /index")', "Gallery must verify / redirects to /index by default in app mode."),
    (gallery, 'Esp32BaseWeb::addPage("/index", "总览", handleStatusPage)', "Gallery must register /index as the default business homepage."),
    (full, 'RUN_SELFTEST("GET", "/", nullptr, true, 200, "<title>Dashboard</title>")', "Full demo must verify explicit root business home renders directly."),
    (full, 'RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "<title>Status</title>")', "Full demo must verify /esp32base remains available."),
    (docs, "业务模式默认优先使用 `/index`", "Web docs must describe /index as the default business home."),
    (changelog, "根路径业务首页", "CHANGELOG must describe root business home routing."),
]

errors = [message for text, needle, message in checks if needle not in text]
if errors:
    for error in errors:
        print(error)
    raise SystemExit(1)

print("Web root home checks passed")
```

- [ ] **Step 2: Add desired example selftests**

In `examples/web_ui_gallery/src/main.cpp`, change the root selftest:

```cpp
RUN_SELFTEST("GET", "/", nullptr, true, 302, "Location: /index");
RUN_SELFTEST("GET", "/index", nullptr, true, 200, "状态概览");
```

In `examples/full_demo/src/main.cpp`, change/add selftests:

```cpp
RUN_SELFTEST("GET", "/", nullptr, true, 200, "<title>Dashboard</title>");
RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "<title>Status</title>");
```

- [ ] **Step 3: Run RED check**

Run:

```bash
python3 scripts/check_web_root_home.py
```

Expected: FAIL with messages about missing route lookup and `/index` default behavior.

- [ ] **Step 4: Commit after green later**

Do not commit during RED. Commit after Task 2 makes this pass.

---

## Task 2: Implement Root Home Routing

**Files:**
- Modify: `src/web/internal/WebRouting.cpp`
- Modify: `src/web/internal/WebInternal.h`
- Modify: `examples/web_ui_gallery/src/main.cpp`
- Modify: `examples/full_demo/src/main.cpp`

- [ ] **Step 1: Add route lookup helpers**

In `src/web/internal/WebRouting.cpp`, add near `routeCount()`:

```cpp
bool routeMatchesMethod(const Route& route, Esp32BaseWeb::Method method) {
    return route.method == Esp32BaseWeb::METHOD_ANY || route.method == method;
}

Route* findRoute(const char* path, Esp32BaseWeb::Method method) {
    if (!path || !path[0]) {
        return nullptr;
    }
    for (uint8_t i = 0; i < ESP32BASE_WEB_MAX_ROUTES; ++i) {
        if (g_routes[i].handler && strcmp(g_routes[i].path, path) == 0 && routeMatchesMethod(g_routes[i], method)) {
            return &g_routes[i];
        }
    }
    return nullptr;
}
```

In `src/web/internal/WebInternal.h`, add after `uint8_t routeCount(bool appPageOnly);`:

```cpp
bool routeMatchesMethod(const Route& route, Esp32BaseWeb::Method method);
Route* findRoute(const char* path, Esp32BaseWeb::Method method);
```

- [ ] **Step 2: Prefer /index when home path is not explicit**

Replace `configuredHomePath()` in `src/web/internal/WebRouting.cpp` with:

```cpp
const char* configuredHomePath() {
    if (g_homePath[0]) {
        return g_homePath;
    }
    for (uint8_t i = 0; i < ESP32BASE_WEB_MAX_NAV_ITEMS; ++i) {
        if (strcmp(g_navItems[i].path, "/index") == 0) {
            return g_navItems[i].path;
        }
    }
    for (uint8_t i = 0; i < ESP32BASE_WEB_MAX_ROUTES; ++i) {
        if (g_routes[i].handler && g_routes[i].appPage && strcmp(g_routes[i].path, "/index") == 0) {
            return g_routes[i].path;
        }
    }
    for (uint8_t i = 0; i < ESP32BASE_WEB_MAX_NAV_ITEMS; ++i) {
        if (g_navItems[i].path[0]) {
            return g_navItems[i].path;
        }
    }
    for (uint8_t i = 0; i < ESP32BASE_WEB_MAX_ROUTES; ++i) {
        if (g_routes[i].handler && g_routes[i].appPage) {
            return g_routes[i].path;
        }
    }
    return "/esp32base";
}
```

- [ ] **Step 3: Keep root route centralized**

Replace `registerRoute()` in `src/web/internal/WebRouting.cpp` with:

```cpp
void dispatchRoute(Route& route) {
    g_requestContextActive = true;
    markRequest();
    if (route.handler) {
        route.handler();
    }
    g_responseActive = false;
    g_requestContextActive = false;
    g_currentMethod = Esp32BaseWeb::METHOD_UNKNOWN;
}

void registerRoute(Route& route) {
    if (strcmp(route.path, "/") == 0 && routeMatchesMethod(route, Esp32BaseWeb::METHOD_GET)) {
        route.registered = true;
        return;
    }
    Route* routePtr = &route;
    g_server.on(route.path, toHttpMethod(route.method), [routePtr]() {
        dispatchRoute(*routePtr);
    });
    route.registered = true;
}
```

Add `void dispatchRoute(Route& route);` to `WebInternal.h` near `registerRoute`.

- [ ] **Step 4: Dispatch explicit root business home**

Replace `handleRootRedirect()` in `src/web/internal/WebRouting.cpp` with:

```cpp
void handleRootRedirect() {
    markRequest();
    const char* location = g_homeMode == Esp32BaseWeb::HOME_ESP32BASE ? "/esp32base" : configuredHomePath();
    if (g_homeMode != Esp32BaseWeb::HOME_ESP32BASE && strcmp(location, "/") == 0) {
        Route* route = findRoute("/", Esp32BaseWeb::METHOD_GET);
        if (route && route->handler) {
            dispatchRoute(*route);
            return;
        }
        location = "/esp32base";
    }
    g_server.sendHeader("Location", location, true);
    g_server.send(302, "text/plain", "");
}
```

- [ ] **Step 5: Update examples**

In `examples/web_ui_gallery/src/main.cpp` setup:

```cpp
// Remove Esp32BaseWeb::setHomePath("/ui-status");
Esp32BaseWeb::addPage("/index", "总览", handleStatusPage);
Esp32BaseWeb::addRoute("/ui-status", Esp32BaseWeb::METHOD_GET, handleStatusPage);
```

In `examples/full_demo/src/main.cpp` setup:

```cpp
Esp32BaseWeb::setHomePath("/");
Esp32BaseWeb::setHomeMode(Esp32BaseWeb::HOME_COMBINED);
...
Esp32BaseWeb::addPage("/", "Dashboard", handleDashboard);
```

- [ ] **Step 6: Run GREEN checks**

Run:

```bash
python3 scripts/check_web_root_home.py
platformio run -d examples/web_ui_gallery -e esp32_web_ui_gallery
platformio run -d examples/full_demo -e esp32_full
```

Expected: all commands exit 0.

- [ ] **Step 7: Commit**

```bash
git add src/web/internal/WebRouting.cpp src/web/internal/WebInternal.h examples/web_ui_gallery/src/main.cpp examples/full_demo/src/main.cpp scripts/check_web_root_home.py
git commit -m "feat(web): support business root home routing"
```

---

## Task 3: RED Checks For Progressive Enhancement Assets

**Files:**
- Modify: `scripts/check_web_ui_baseline.py`
- Modify: `examples/web_ui_gallery/src/main.cpp`

- [ ] **Step 1: Extend static checks**

Add these checks to `scripts/check_web_ui_baseline.py`:

```python
enhancement_checks = [
    (assets, "function ebAjaxSubmit", "WEB_HEAD must include the AJAX submit runtime."),
    (assets, ".eb-dialog{", "CSS must include dialog shell styles."),
    (assets, ".eb-inline-edit{", "CSS must include inline edit styles."),
    (web, "sendInfoRowInlineEdit", "Esp32BaseWeb must expose inline edit helper implementation."),
    (web, "sendInfoRowDialogForm", "Esp32BaseWeb must expose dialog form helper implementation."),
    (web, "sendAjaxReplace", "Esp32BaseWeb must expose AJAX replace JSON helper."),
]
for text, needle, message in enhancement_checks:
    if needle not in text:
        errors.append(message)
```

- [ ] **Step 2: Add desired gallery selftests**

In `examples/web_ui_gallery/src/main.cpp`, add selftests:

```cpp
RUN_SELFTEST("GET", "/esp32base/ui.css", nullptr, true, 200, ".eb-dialog{");
RUN_SELFTEST("GET", "/esp32base/ui.css", nullptr, true, 200, ".eb-inline-edit{");
RUN_SELFTEST("GET", "/index", nullptr, true, 200, "data-eb-inline-edit");
RUN_SELFTEST("GET", "/ui-config", nullptr, true, 200, "data-eb-dialog");
RUN_SELFTEST("POST", "/ui-config/name", "name=flower", true, 200, "\"ok\":true");
RUN_SELFTEST("POST", "/ui-config/name", "name=", true, 400, "\"ok\":false");
RUN_SELFTEST("POST", "/ui-config/dialog", "limit=42&mode=manual", true, 200, "\"ok\":true");
```

The AJAX POST tests need a request helper variant that sends `X-Esp32Base-Ajax: 1` and `Accept: application/json`; add it in Task 5.

- [ ] **Step 3: Run RED check**

Run:

```bash
python3 scripts/check_web_ui_baseline.py
```

Expected: FAIL with missing AJAX/dialog/inline markers.

---

## Task 4: Implement CSS And JavaScript Runtime

**Files:**
- Modify: `src/web/internal/WebAssets.cpp`

- [ ] **Step 1: Add CSS**

Append these rules into `WEB_CSS` before `.kv{...}`:

```cpp
".eb-inline-edit{border:1px solid #cbdde5;background:#f7fafb;border-radius:8px;padding:10px;margin:8px 0}.eb-inline-edit .actions{justify-content:flex-end}.eb-inline-error,.eb-dialog-error{display:none;color:var(--eb-danger);font-size:13px;margin:6px 0 0}.eb-inline-error.show,.eb-dialog-error.show{display:block}.eb-dialog-backdrop{position:fixed;inset:0;background:rgba(15,23,42,.28);display:none;align-items:center;justify-content:center;padding:18px;z-index:50}.eb-dialog-backdrop.open{display:flex}.eb-dialog{width:min(440px,100%);background:var(--eb-surface);border:1px solid var(--eb-line);border-radius:8px;box-shadow:0 18px 46px rgba(16,24,40,.18);padding:14px}.eb-dialog h2{font-size:16px;margin-bottom:8px}.eb-dialog .actions{justify-content:flex-end}.eb-dialog-close{float:right;background:var(--eb-button-soft);color:#344054;border:1px solid var(--eb-button-border);min-height:28px}.eb-busy{opacity:.7}"
```

Extend mobile media rule with:

```css
.eb-dialog-backdrop{align-items:flex-end;padding:10px}.eb-dialog{width:100%;max-height:92vh;overflow:auto}
```

- [ ] **Step 2: Add small JS runtime**

Replace `WEB_HEAD` script with a script that keeps `once()` and adds `ebAjaxSubmit`, `ebOpenDialog`, `ebCloseDialog`, and event delegation. The script must:

- return `true` from `once(f)` for fallback forms;
- intercept only `form[data-eb-ajax]`;
- set headers `X-Esp32Base-Ajax: 1` and `Accept: application/json`;
- replace `document.getElementById(json.target).outerHTML` with `json.html`;
- close the nearest `.eb-dialog-backdrop` when `json.close` is true;
- show errors in `[data-eb-error]`.

Use this compact script body:

```cpp
"<script>function once(f){if(f.dataset.busy)return false;f.dataset.busy=1;var b=f.querySelector('[type=submit]');if(b)b.disabled=true;return true;}function ebErr(box,msg){if(!box)return;box.textContent=msg||'Action failed';box.classList.add('show');}function ebOpenDialog(id){var d=document.getElementById(id);if(d){d.classList.add('open');var i=d.querySelector('input,select,button');if(i)i.focus();}}function ebCloseDialog(el){var d=el&&el.closest?el.closest('.eb-dialog-backdrop'):null;if(d)d.classList.remove('open');}function ebAjaxSubmit(f){if(!window.fetch)return true;if(f.dataset.busy)return false;f.dataset.busy=1;f.classList.add('eb-busy');var b=f.querySelector('[type=submit]'),e=f.querySelector('[data-eb-error]');if(b)b.disabled=true;if(e){e.textContent='';e.classList.remove('show');}fetch(f.action,{method:f.method||'POST',headers:{'X-Esp32Base-Ajax':'1','Accept':'application/json','Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(new FormData(f)).toString(),credentials:'same-origin'}).then(function(r){return r.json().then(function(j){j.status=r.status;return j;});}).then(function(j){if(!j.ok){ebErr(e,j.error);return;}if(j.target&&j.html){var t=document.getElementById(j.target);if(t)t.outerHTML=j.html;}if(j.close)ebCloseDialog(f);}).catch(function(){ebErr(e,'Request failed.');}).finally(function(){delete f.dataset.busy;f.classList.remove('eb-busy');if(b)b.disabled=false;});return false;}document.addEventListener('submit',function(ev){var f=ev.target;if(f&&f.matches&&f.matches('form[data-eb-ajax]')){ev.preventDefault();ebAjaxSubmit(f);}});document.addEventListener('click',function(ev){var t=ev.target;if(t.matches('[data-eb-dialog-open]')){ev.preventDefault();ebOpenDialog(t.getAttribute('data-eb-dialog-open'));}if(t.matches('[data-eb-dialog-close]')){ev.preventDefault();ebCloseDialog(t);}});document.addEventListener('keydown',function(ev){if(ev.key=='Escape'){document.querySelectorAll('.eb-dialog-backdrop.open').forEach(function(d){d.classList.remove('open');});}});</script>"
```

- [ ] **Step 3: Run static check**

Run:

```bash
python3 scripts/check_web_ui_baseline.py
```

Expected: still FAIL because C++ helpers and gallery examples are not implemented yet.

---

## Task 5: Add Web Helper APIs

**Files:**
- Modify: `src/web/Esp32BaseWeb.h`
- Modify: `src/web/Esp32BaseWeb.cpp`

- [ ] **Step 1: Add declarations**

In `Esp32BaseWeb.h`, after `sendInfoRowCompactForm(...)`, add:

```cpp
static void sendInfoRowInlineEdit(const char* id, const char* title, const char* help, const char* value,
                                  const char* action, const char* inputName, const char* inputValue,
                                  const char* label = "Edit", UiTone tone = UI_INFO);
static void sendInfoRowDialogForm(const char* dialogId, const char* targetId, const char* title,
                                  const char* help, const char* value, const char* action,
                                  const char* label = "Edit", UiTone tone = UI_INFO);
static bool isAjaxRequest();
static void sendAjaxReplace(const char* targetId, const char* html, const char* noticeTitle = nullptr,
                            UiTone tone = UI_OK, bool close = true);
static void sendAjaxError(int code, const char* error);
```

- [ ] **Step 2: Add implementations**

In `Esp32BaseWeb.cpp`, after `sendInfoRowCompactForm(...)`, add implementations that:

- emit a wrapper with `id`;
- use `data-eb-inline-edit` for inline rows;
- emit `<form data-eb-ajax ...>`;
- emit `<div data-eb-error class='eb-inline-error'></div>`;
- emit dialog opener and a hidden `.eb-dialog-backdrop`.

Use `g_server.header("X-Esp32Base-Ajax") == "1"` for `isAjaxRequest()`.

For `sendAjaxReplace()`, emit JSON:

```json
{"ok":true,"target":"...","html":"...","notice":{"tone":"ok","title":"..."},"close":true}
```

Use `sendEscapedJsonChunk()` for dynamic strings.

For `sendAjaxError()`, emit:

```json
{"ok":false,"error":"..."}
```

- [ ] **Step 3: Run static check**

Run:

```bash
python3 scripts/check_web_ui_baseline.py
```

Expected: still FAIL until gallery examples call the new helpers.

---

## Task 6: Gallery Example AJAX Inline And Dialog

**Files:**
- Modify: `examples/web_ui_gallery/src/main.cpp`

- [ ] **Step 1: Add AJAX-capable request helper**

Change `selfTestRequest(...)` signature to include `bool ajax = false`. If `ajax` is true, send:

```cpp
client.print("X-Esp32Base-Ajax: 1\r\nAccept: application/json\r\n");
```

Add macro:

```cpp
#define RUN_AJAX_SELFTEST(method, path, body, auth, code, contains, ...) \
    do { ++total; if (selfTestRequest(method, path, body, auth, code, contains, ##__VA_ARGS__, true)) ++pass; } while (0)
```

If variadic ordering becomes awkward, add a second function `selfTestAjaxRequest()` that calls the shared implementation with `ajax=true`.

- [ ] **Step 2: Render inline row and dialog**

In the config page, replace the old `?edit=name` full-page inline block with:

```cpp
Esp32BaseWeb::sendInfoRowInlineEdit("row-channel-name", "第 1 路名称",
                                    "用于页面和记录展示，不影响实际控制。", "花坛",
                                    "/ui-config/name", "name", "花坛", "编辑");
Esp32BaseWeb::sendInfoRowDialogForm("dlg-plan", "row-plan", "默认计划",
                                    "包含时间、通道、执行天数和目标量。", "3 条",
                                    "/ui-config/dialog", "快速编辑");
```

Add a simple dialog body after the row if the helper only emits the shell opener; fields:

```html
<input name='limit' type='number' min='1' max='120' value='30'>
<select name='mode'><option value='auto'>auto</option><option value='manual'>manual</option></select>
```

- [ ] **Step 3: Update POST handlers**

Change `handleConfigNameSave()`:

- if name is empty, AJAX returns `sendAjaxError(400, "Name is required.")`;
- if AJAX succeeds, render the updated row into a small buffer or send a fixed escaped fragment and call `sendAjaxReplace("row-channel-name", html, "Saved")`;
- if not AJAX, keep `redirectSeeOther("/ui-config?saved=1")`.

Add `handleConfigDialogSave()`:

- validate `limit` is 1-120 and `mode` is `auto` or `manual`;
- AJAX success replaces `row-plan`;
- fallback success redirects `/ui-config?saved=1`.

- [ ] **Step 4: Register dialog POST route**

Add:

```cpp
Esp32BaseWeb::addRoute("/ui-config/dialog", Esp32BaseWeb::METHOD_POST, handleConfigDialogSave);
```

- [ ] **Step 5: Run checks and builds**

Run:

```bash
python3 scripts/check_web_ui_baseline.py
platformio run -d examples/web_ui_gallery -e esp32_web_ui_gallery
```

Expected: both pass.

---

## Task 7: Docs And Changelog

**Files:**
- Modify: `README.md`
- Modify: `docs/03_api.md`
- Modify: `docs/04_web.md`
- Modify: `docs/11_web_ui_baseline.md`
- Modify: `CHANGELOG.md`
- Modify: `docs/superpowers/specs/2026-05-31-web-ui-progressive-enhancement-design.md`

- [ ] **Step 1: Update root home docs**

Document:

- `HOME_ESP32BASE`: `/` redirects to `/esp32base`.
- `HOME_APP` / `HOME_COMBINED`: if no explicit `setHomePath()` is set and `/index` is registered, `/` redirects to `/index`.
- explicit `setHomePath("/")` dispatches a registered GET root business handler directly.
- `/esp32base` remains the system/fusion entry.

- [ ] **Step 2: Update AJAX/dialog docs**

Document:

- single-field inline edit helper;
- small dialog form helper;
- AJAX request header `X-Esp32Base-Ajax: 1`;
- JSON success/error shape;
- high-risk operations excluded from default local refresh.

- [ ] **Step 3: Update CHANGELOG**

Add sections:

```markdown
### 根路径业务首页
### Web UI 局部交互与小表单弹层
```

- [ ] **Step 4: Run doc/static checks**

Run:

```bash
python3 scripts/check_web_root_home.py
python3 scripts/check_web_ui_baseline.py
```

Expected: both pass.

- [ ] **Step 5: Commit**

```bash
git add README.md docs/03_api.md docs/04_web.md docs/11_web_ui_baseline.md CHANGELOG.md docs/superpowers/specs/2026-05-31-web-ui-progressive-enhancement-design.md
git commit -m "docs(web): describe root home and local interactions"
```

---

## Task 8: Final Verification

**Files:**
- No new code changes unless verification exposes failures.

- [ ] **Step 1: Run all relevant static checks**

Run:

```bash
python3 scripts/check_web_root_home.py
python3 scripts/check_web_ui_baseline.py
python3 scripts/check_footer_bar_modes.py
python3 scripts/check_web_route_defaults.py
```

Expected: all pass.

- [ ] **Step 2: Run PlatformIO builds**

Run:

```bash
platformio run -d examples/web_ui_gallery -e esp32_web_ui_gallery
platformio run -d examples/full_demo -e esp32_full
```

Expected: both pass.

- [ ] **Step 3: Optional browser visual check**

If a local or device URL is available, open:

- `/index` for the gallery default home redirect target.
- `/ui-config` for inline edit and dialog appearance.
- `/` on full demo for direct Dashboard render.
- `/esp32base` on full demo for system/fusion page.

Verify no button overflow, no full-page flash on AJAX save, and dialog is compact.

- [ ] **Step 4: Commit final adjustments**

If verification required fixes:

```bash
git add <changed-files>
git commit -m "fix(web): verify local interactions and root home"
```

If no fixes were needed, do not create an empty commit.
