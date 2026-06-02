#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def main() -> int:
    webota = read("scripts/esp32base_webota.py")
    ota_docs = read("docs/05_ota.md")
    errors: list[str] = []

    if '_option("esp32base_webota_upload_timeout"), 90.0)' not in webota:
        errors.append("scripts/esp32base_webota.py: default upload timeout must be 90 seconds")
    if "`esp32base_webota_upload_timeout`：默认 `90` 秒" not in ota_docs:
        errors.append("docs/05_ota.md: webota upload timeout default must document 90 seconds")
    if "auth_header = _auth_header()" not in webota or '"Authorization": auth_header' not in webota:
        errors.append("scripts/esp32base_webota.py: auth header validation must be handled before request headers are built")

    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1
    print("Web OTA default checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
