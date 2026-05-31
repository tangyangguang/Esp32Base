from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def require(text: str, needle: str, message: str, errors: list[str]) -> None:
    if needle not in text:
        errors.append(message)


WEB_SOURCE_PATHS = [
    "src/web/Esp32BaseWeb.cpp",
    "src/web/internal/WebInternal.h",
    "src/web/internal/WebContext.h",
    "src/web/internal/WebContext.cpp",
    "src/web/internal/WebAssets.cpp",
    "src/web/internal/WebAuth.cpp",
    "src/web/internal/WebFs.cpp",
    "src/web/internal/WebLayout.cpp",
    "src/web/internal/WebLogs.cpp",
    "src/web/internal/WebOta.cpp",
    "src/web/internal/WebResponse.cpp",
    "src/web/internal/WebRouting.cpp",
    "src/web/internal/WebStatus.cpp",
    "src/web/internal/WebTools.cpp",
    "src/web/internal/WebWifi.cpp",
    "src/web/internal/WebAppConfig.cpp",
]

def read_web_source() -> str:
    return "\n".join((ROOT / path).read_text(encoding="utf-8") for path in WEB_SOURCE_PATHS)

def main() -> int:
    ota = (ROOT / "src/update/Esp32BaseOta.inc").read_text()
    ota_h = (ROOT / "src/update/Esp32BaseOta.h").read_text()
    web = read_web_source()
    docs = (ROOT / "docs/05_ota.md").read_text()
    readme = (ROOT / "README.md").read_text()
    changelog = (ROOT / "CHANGELOG.md").read_text()
    recover_path = ROOT / "scripts/esp32base_serial_recover_ota.py"
    recover = recover_path.read_text() if recover_path.exists() else ""

    errors: list[str] = []

    require(ota_h, "calculatedSha256()", "Esp32BaseOta must expose calculated SHA256", errors)
    require(ota_h, "lastTargetPartitionLabel()", "Esp32BaseOta must expose last OTA target partition", errors)
    require(ota_h, "lastBootPartitionLabel()", "Esp32BaseOta must expose post-update boot partition", errors)
    require(ota, "esp_ota_get_boot_partition()", "OTA success path must read configured boot partition", errors)
    require(ota, "boot partition mismatch", "OTA success path must fail on boot partition mismatch", errors)
    require(ota, "esp_ota_get_app_elf_sha256", "OTA begin must log running firmware ELF SHA256", errors)
    require(ota, "upload_target", "OTA start log must identify the write target slot", errors)
    require(web, "calculatedSha256", "/esp32base/api/ota must return calculatedSha256", errors)
    require(web, "\"runningPartition\"", "/esp32base/api/ota must return runningPartition", errors)
    require(web, "\"bootPartition\"", "/esp32base/api/ota must return bootPartition", errors)
    require(web, "appPartitions", "/esp32base/api/ota must return appPartitions diagnostics", errors)
    require(web, "parseSizeHeader", "Web OTA must strictly parse X-Firmware-Size", errors)
    require(recover, "esp32base_serial_recover_ota", "serial recovery script must exist with a clear program name", errors)
    require(recover, "--clear-otadata", "serial recovery script must support clearing otadata", errors)
    require(recover, "--write-both-ota", "serial recovery script must support writing both OTA slots", errors)
    require(recover, "gen_esp32part.py", "serial recovery script must parse PlatformIO partition tables", errors)
    require(docs, "双 OTA 串口恢复", "OTA docs must document dual OTA serial recovery", errors)
    require(readme, "esp32base_serial_recover_ota.py", "README must mention the serial recovery script", errors)
    require(changelog, "双 OTA 串口恢复", "CHANGELOG must record dual OTA recovery changes", errors)

    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1
    print("OTA recovery diagnostics checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
