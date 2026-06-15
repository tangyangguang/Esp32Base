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
    if '_option("esp32base_webota_path", "/esp32base/ota/raw")' not in webota:
        errors.append("scripts/esp32base_webota.py: default upload path must use raw /esp32base/ota/raw")
    if '_option("esp32base_webota_chunk_size"), 64 * 1024)' not in webota:
        errors.append("scripts/esp32base_webota.py: default chunk size must be 65536 bytes")
    if "`esp32base_webota_path`：默认 `/esp32base/ota/raw`" not in ota_docs:
        errors.append("docs/05_ota.md: webota default raw path must be documented")
    if "`esp32base_webota_chunk_size`：默认 `65536` 字节" not in ota_docs:
        errors.append("docs/05_ota.md: webota chunk size default must document 65536 bytes")
    if "脚本默认 raw endpoint 使用 65536 字节分块" not in ota_docs:
        errors.append("docs/05_ota.md: raw Web OTA default chunk guidance must be documented")
    if "auth_header = _auth_header()" not in webota or '"Authorization": auth_header' not in webota:
        errors.append("scripts/esp32base_webota.py: auth header validation must be handled before request headers are built")
    if "HTTP_RAW_BUFLEN = 1436" not in webota:
        errors.append("scripts/esp32base_webota.py: raw upload padding must use Arduino WebServer HTTP_RAW_BUFLEN")
    if "_raw_padded_size(firmware_size)" not in webota:
        errors.append("scripts/esp32base_webota.py: raw upload must pad Content-Length to avoid final short-read timeout")
    if "request_headers[\"Content-Length\"] = str(padded_size)" not in webota:
        errors.append("scripts/esp32base_webota.py: raw upload Content-Length must include padding")
    if "sock.sendall(b\"\\0\" * padding_size)" not in webota:
        errors.append("scripts/esp32base_webota.py: raw upload must send zero padding bytes")
    if "sock.sendall(request_head + first_chunk)" not in webota:
        errors.append("scripts/esp32base_webota.py: raw upload must send request headers and first firmware bytes in one socket write")
    if "first_chunk = fh.read(chunk_size)" not in webota:
        errors.append("scripts/esp32base_webota.py: raw upload must read an initial body chunk before ending headers")
    if "connection.endheaders(first_chunk)" in webota:
        errors.append("scripts/esp32base_webota.py: raw upload must not rely on HTTPConnection.endheaders(first_chunk)")
    if "def _send_raw_aligned" in webota or "raw_send_carry" in webota:
        errors.append("scripts/esp32base_webota.py: raw upload must not split every socket send into HTTP_RAW_BUFLEN-sized writes")
    if "esp32base_webota_raw_pause_ms" not in webota:
        errors.append("scripts/esp32base_webota.py: raw upload must expose an inter-chunk pacing option")
    if "RAW_RSSI_VERY_WEAK_DBM" not in webota or "RAW_RSSI_WEAK_DBM" not in webota:
        errors.append("scripts/esp32base_webota.py: raw upload must define weak RSSI thresholds")
    if "_resolve_raw_transfer_settings(" not in webota:
        errors.append("scripts/esp32base_webota.py: raw upload must auto-adjust chunk size and pacing from RSSI")
    if "time.sleep(raw_pause_ms / 1000.0)" not in webota:
        errors.append("scripts/esp32base_webota.py: raw upload must sleep between paced raw chunks")
    if "esp32base_webota_socket_send_buffer" not in webota or "SO_SNDBUF" not in webota:
        errors.append("scripts/esp32base_webota.py: raw upload must expose socket send-buffer control")
    if "TCP_NODELAY" not in webota:
        errors.append("scripts/esp32base_webota.py: raw upload sockets must disable Nagle for paced writes")
    if "esp32base_webota_fallback_to_multipart" in webota:
        errors.append("scripts/esp32base_webota.py: raw upload must not expose automatic multipart fallback")
    if "_with_upload_path(parsed, \"/esp32base/ota\")" in webota:
        errors.append("scripts/esp32base_webota.py: raw upload must not retry through the multipart path")
    if "Web OTA fallback: raw upload interrupted" in webota:
        errors.append("scripts/esp32base_webota.py: raw upload must not hide failures behind multipart fallback")
    if "_preflight(parsed, headers, timeout, verify_tls, firmware_size)" not in webota:
        errors.append("scripts/esp32base_webota.py: preflight must receive local firmware size")
    if "_validate_remote_ota_capacity(payload, firmware_size)" not in webota:
        errors.append("scripts/esp32base_webota.py: preflight must reject firmware larger than next OTA partition")
    if "Web OTA blocked before upload" not in webota:
        errors.append("scripts/esp32base_webota.py: oversize preflight error must clearly say upload was not sent")
    if "预检还会比较本地固件大小和设备下一 OTA 分区容量，超出时不会发送固件 body" not in ota_docs:
        errors.append("docs/05_ota.md: webota preflight must document size check before sending firmware body")
    if "raw padding 依赖当前 Arduino ESP32 `WebServer` 的 `HTTP_RAW_BUFLEN=1436`" not in ota_docs:
        errors.append("docs/05_ota.md: raw padding must document Arduino HTTP_RAW_BUFLEN maintenance constraint")
    if "本项目暂不在上传脚本中动态解析 Arduino core 头文件" not in ota_docs:
        errors.append("docs/05_ota.md: raw padding must document the intentional fixed-constant strategy")
    if "已按源码核对 Arduino ESP32 `2.0.16`、`2.0.17`、`3.0.0`、`3.0.7`、`3.3.0`" not in ota_docs:
        errors.append("docs/05_ota.md: raw padding must document checked Arduino ESP32 core versions")
    if "该风险只影响 raw endpoint；浏览器表单上传和显式 `/esp32base/ota` multipart 路径不依赖 `HTTPRaw`" not in ota_docs:
        errors.append("docs/05_ota.md: raw padding risk must document the parser boundary")
    if "raw 上传失败不会完成 OTA boot 分区切换，设备应保持原固件运行" not in ota_docs:
        errors.append("docs/05_ota.md: raw padding risk must document failed raw OTA boot behavior")
    if "脚本不把每次 socket send 强行切成 1436 字节" not in ota_docs:
        errors.append("docs/05_ota.md: raw upload pacing must document padding without 1436-byte socket send slicing")
    if "raw 请求头会和第一段固件 body 在同一次 socket 写中发送" not in ota_docs:
        errors.append("docs/05_ota.md: raw upload must document first body bytes sent with headers")
    if "`esp32base_webota_raw_pause_ms`" not in ota_docs:
        errors.append("docs/05_ota.md: raw upload pacing option must be documented")
    if "`esp32base_webota_socket_send_buffer`" not in ota_docs:
        errors.append("docs/05_ota.md: raw socket send-buffer option must be documented")
    if "不会在 raw 失败后自动改走 multipart" not in ota_docs:
        errors.append("docs/05_ota.md: raw command must document that it does not auto-fallback to multipart")
    if "RSSI 低于 -70 dBm" not in ota_docs or "RSSI 低于 -75 dBm" not in ota_docs:
        errors.append("docs/05_ota.md: raw upload weak RSSI auto pacing must be documented")

    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1
    print("Web OTA default checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
