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
    recover_path = ROOT / "scripts/esp32base_serial_recover_ota.py"
    recover = recover_path.read_text() if recover_path.exists() else ""
    recover_check = (ROOT / "scripts/check_serial_recover_ota.py").read_text()
    boot_safety_check = (ROOT / "scripts/check_ota_boot_safety.py").read_text()

    errors: list[str] = []

    require(ota_h, "calculatedSha256()", "Esp32BaseOta must expose calculated SHA256", errors)
    require(ota_h, "beginNetworkServices()", "Esp32BaseOta must split early boot init from network services", errors)
    require(ota_h, "lastTargetPartitionLabel()", "Esp32BaseOta must expose last OTA target partition", errors)
    require(ota_h, "lastBootPartitionLabel()", "Esp32BaseOta must expose post-update boot partition", errors)
    require(ota, "esp_ota_get_boot_partition()", "OTA success path must read configured boot partition", errors)
    require(ota, "boot partition mismatch", "OTA success path must fail on boot partition mismatch", errors)
    require(ota, "esp_ota_get_app_elf_sha256", "OTA begin must log running firmware ELF SHA256", errors)
    require(ota, "boot_summary running_partition", "OTA begin must log one-line boot slot summary", errors)
    require(ota, "next_update_partition", "OTA begin must include next OTA update partition", errors)
    require(ota, "stage=%s error=%s", "OTA failures must include current OTA stage", errors)
    require(ota, "bytes=%s total=%s", "OTA failures must include processed and total bytes", errors)
    require(ota, "stage=write", "OTA progress logs must identify write stage", errors)
    require(ota, "logUploadStage(\"verify\")", "OTA finish path must log verify stage", errors)
    require(ota, "logUploadStage(\"set_boot\")", "OTA finish path must log boot-switch stage", errors)
    require(ota, "upload_stage stage=boot_confirmed", "OTA finish path must log boot partition confirmation", errors)
    require(ota, "upload_target", "OTA start log must identify the write target slot", errors)
    require(web, "calculatedSha256", "/esp32base/api/ota must return calculatedSha256", errors)
    require(web, "\"runningPartition\"", "/esp32base/api/ota must return runningPartition", errors)
    require(web, "\"bootPartition\"", "/esp32base/api/ota must return bootPartition", errors)
    require(web, "appPartitions", "/esp32base/api/ota must return appPartitions diagnostics", errors)
    require(web, "parseSizeHeader", "Web OTA must strictly parse X-Firmware-Size", errors)
    require(web, 'formatOtaAbortReason("/esp32base/ota/raw", "client aborted raw upload"', "Raw Web OTA aborts must identify the raw endpoint", errors)
    require(web, "formatOtaAbortReason", "Web OTA aborts must include processed and total byte diagnostics", errors)
    require(web, "accepted >= total", "Raw Web OTA must ignore data after declared firmware size", errors)
    require(web, "remaining = total - accepted", "Raw Web OTA must cap writes at declared firmware size including buffered bytes", errors)
    require(web, "raw.currentSize > remaining", "Raw Web OTA must ignore transport padding beyond firmware size", errors)
    require(web, "kRawOtaFlashWriteChunkSize", "Raw Web OTA must split buffered flash writes into watchdog-friendly chunks", errors)
    require(web, "fw.onchange", "Web OTA page must update file size immediately when a firmware file is selected", errors)
    require(web, "Selected firmware ", "Web OTA page must show selected firmware size before upload starts", errors)
    require(recover, "esp32base_serial_recover_ota", "serial recovery script must exist with a clear program name", errors)
    require(recover, "--clear-otadata", "serial recovery script must support clearing otadata", errors)
    require(recover, "--write-both-ota", "serial recovery script must support writing both OTA slots", errors)
    require(recover, "otadata-erased-0xff.bin", "serial recovery script must clear otadata inside write_flash", errors)
    require(recover, "Recovery write_flash failed", "serial recovery script must explain failed recovery state", errors)
    require(recover, "boot_app0: skipped because it overlaps otadata being cleared", "serial recovery script must avoid duplicate boot_app0/otadata writes", errors)
    require(recover, "Serial port appears busy", "serial recovery script must report serial monitor port contention", errors)
    require(recover, "gen_esp32part.py", "serial recovery script must parse PlatformIO partition tables", errors)
    require(recover_check, "0x19000", "serial recovery dry-run check must cover ESP32_Faucet otadata offset", errors)
    require(recover_check, "0xe000", "serial recovery dry-run check must cover boot_app0/otadata overlap", errors)
    require(recover_check, "20K", "serial recovery dry-run check must cover K/M partition sizes", errors)
    require(recover_check, "erase_region", "serial recovery dry-run check must reject second erase_region command", errors)
    require(recover_check, "serial_port_busy_report", "serial recovery checks must cover busy serial port diagnostics", errors)
    require(boot_safety_check, "Esp32BaseOta::begin() must run during Esp32Base::begin()", "OTA boot safety check must enforce early rollback initialization", errors)
    require(docs, "双 OTA 串口恢复", "OTA docs must document dual OTA serial recovery", errors)
    require(docs, "同一次 `write_flash`", "OTA docs must document same-command otadata clearing", errors)
    require(readme, "esp32base_serial_recover_ota.py", "README must mention the serial recovery script", errors)

    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1
    print("OTA recovery diagnostics checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
