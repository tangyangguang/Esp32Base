from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    base = (ROOT / "src/Esp32Base.cpp").read_text(encoding="utf-8")
    ota_h = (ROOT / "src/update/Esp32BaseOta.h").read_text(encoding="utf-8")
    ota = (ROOT / "src/update/Esp32BaseOta.inc").read_text(encoding="utf-8")
    errors: list[str] = []

    begin_start = base.find("bool Esp32Base::begin()")
    begin_end = base.find("void Esp32Base::handle()", begin_start)
    handle_start = begin_end
    handle_end = base.find("void Esp32Base::setFirmwareInfo", handle_start)
    begin_body = base[begin_start:begin_end] if begin_start >= 0 and begin_end > begin_start else ""
    handle_body = base[handle_start:handle_end] if handle_start >= 0 and handle_end > handle_start else ""

    if "Esp32BaseOta::begin()" not in begin_body:
        errors.append("Esp32BaseOta::begin() must run during Esp32Base::begin(), before Web/WiFi readiness gates")
    if "Esp32BaseWeb::isReady() && !Esp32BaseOta::isReady()" in handle_body:
        errors.append("OTA boot/rollback readiness must not be gated on Web ready")
    if "beginNetworkServices()" not in ota_h or "Esp32BaseOta::beginNetworkServices()" not in ota:
        errors.append("OTA network services must be split from early boot/rollback initialization")
    if "ESP32BASE_OTA_REQUIRE_MARK_VALID" not in begin_body:
        errors.append("Early OTA begin must be documented in code near the boot/rollback initialization path")

    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1
    print("OTA boot safety checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
