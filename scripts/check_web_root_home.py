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
    (
        routing,
        'findRoute("/", Esp32BaseWeb::METHOD_GET)',
        "Root handler must be able to find a GET business route for '/'.",
    ),
    (
        routing,
        'strcmp(configuredHomePath(), "/") == 0',
        "Root handler must special-case explicit home path '/'.",
    ),
    (
        routing,
        'strcmp(g_routes[i].path, "/index") == 0',
        "Default business home must prefer a registered /index app page.",
    ),
    (
        routing,
        'g_homeMode == Esp32BaseWeb::HOME_ESP32BASE ? "/esp32base"',
        "HOME_ESP32BASE must keep redirecting / to /esp32base.",
    ),
    (
        gallery,
        'RUN_SELFTEST("GET", "/", nullptr, true, 302, "Location: /index")',
        "Gallery must verify / redirects to /index by default in app mode.",
    ),
    (
        gallery,
        'Esp32BaseWeb::addPage("/index", "总览", handleStatusPage)',
        "Gallery must register /index as the default business homepage.",
    ),
    (
        full,
        'RUN_SELFTEST("GET", "/", nullptr, true, 200, "<title>Dashboard</title>")',
        "Full demo must verify explicit root business home renders directly.",
    ),
    (
        full,
        'RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "<title>Status</title>")',
        "Full demo must verify /esp32base remains available.",
    ),
    (
        docs,
        "业务模式默认优先使用 `/index`",
        "Web docs must describe /index as the default business home.",
    ),
    (
        changelog,
        "根路径业务首页",
        "CHANGELOG must describe root business home routing.",
    ),
]

errors = [message for text, needle, message in checks if needle not in text]
if errors:
    for error in errors:
        print(error)
    raise SystemExit(1)

print("Web root home checks passed")
