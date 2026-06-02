#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def main() -> int:
    ota = read("src/update/Esp32BaseOta.inc")
    web_ota = read("src/web/internal/WebOta.cpp")
    errors: list[str] = []

    if "g_otaLongOpsActive" not in ota:
        errors.append("src/update/Esp32BaseOta.inc: OTA cleanup must be guarded by an active long-operation flag")
    if "if (!g_otaLongOpsActive)" not in ota:
        errors.append("src/update/Esp32BaseOta.inc: cleanupLongOps must no-op before prepareLongOps")
    if "g_otaLongOpsActive = true;" not in ota or "g_otaLongOpsActive = false;" not in ota:
        errors.append("src/update/Esp32BaseOta.inc: prepare/cleanup must maintain the active flag")

    for marker in (
        'Esp32BaseOta::abortUpload("unauthorized ota upload")',
        'Esp32BaseOta::abortUpload("forbidden ota upload origin")',
        'Esp32BaseOta::abortUpload("unauthorized ota raw upload")',
        'Esp32BaseOta::abortUpload("forbidden ota raw upload origin")',
    ):
        if marker in web_ota:
            errors.append(f"src/web/internal/WebOta.cpp: pre-start rejection must not call {marker}")

    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1
    print("OTA pre-start cleanup checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
