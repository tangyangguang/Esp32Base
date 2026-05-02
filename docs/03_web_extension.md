# Web 扩展说明

## 1. 注册应用页面

应用页面使用普通函数指针注册：

```cpp
void handleHome() {
    Esp32BaseWeb::sendHtml(200, "<h1>Hello</h1>");
}

void setup() {
    Esp32BaseWeb::addPage("/", handleHome);
    Esp32Base::begin();
}
```

`addPage()` 默认处理 GET 请求。

## 2. 注册应用 API

```cpp
void handlePing() {
    Esp32BaseWeb::sendJson(200, "{\"ok\":true}");
}

Esp32BaseWeb::addApi("/api/ping", handlePing);
```

`addApi()` 接受任意 HTTP method。需要限定 method 时使用：

```cpp
Esp32BaseWeb::addRoute("/api/save", Esp32BaseWeb::METHOD_POST, handleSave);
```

## 3. 认证边界

Web Auth 默认开启。内置路由和应用自定义路由共用同一个 Basic Auth。

默认开发账号密码：

```text
admin / admin123
```

可以通过代码设置：

```cpp
Esp32BaseWeb::setAuth("user", "pass");
```

也可以通过宏覆盖：

```ini
build_flags =
    -DESP32BASE_WEB_AUTH_USER=\"admin\"
    -DESP32BASE_WEB_AUTH_PASS=\"change-me\"
```

## 4. 输出规则

推荐短 JSON 和 PROGMEM HTML：

```cpp
static const char kPage[] PROGMEM = "<h1>Page</h1>";

void handlePage() {
    Esp32BaseWeb::sendContentP(kPage);
}
```

第一版不做模板引擎、WebSocket、大型前端资源打包或文件管理平台。

## 5. 路由容量

应用自定义路由固定容量由宏控制：

```text
ESP32BASE_WEB_MAX_APP_ROUTES
```

默认值是 16。路径长度默认小于 48 字节。
