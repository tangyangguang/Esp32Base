#!/usr/bin/env python3
import io
import importlib.util
import tempfile
from pathlib import Path
from unittest import mock
from urllib.parse import urlparse


ROOT = Path(__file__).resolve().parents[1]
WEBOTA_PATH = ROOT / "scripts" / "esp32base_webota.py"


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def load_webota_module():
    spec = importlib.util.spec_from_file_location("esp32base_webota_check", WEBOTA_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError("unable to load esp32base_webota.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def require(condition: bool, message: str, errors: list[str]) -> None:
    if not condition:
        errors.append(message)


class FakeRawSocket:
    def __init__(self) -> None:
        self.writes: list[bytes] = []
        self.closed = False

    def sendall(self, data: bytes) -> None:
        self.writes.append(data)

    def makefile(self, *args, **kwargs):
        return io.BytesIO(b"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK")

    def close(self) -> None:
        self.closed = True


def check_raw_upload_send_contract(webota_module, errors: list[str]) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        firmware = Path(tmp) / "firmware.bin"
        firmware.write_bytes(b"abcdefg")
        fake_socket = FakeRawSocket()
        stats: dict[str, object] = {}
        parsed = urlparse("http://esp32.local/esp32base/ota/raw")
        headers = {
            "Authorization": "Basic dGVzdDp0ZXN0",
            "X-Firmware-Size": "7",
        }
        with mock.patch.object(webota_module, "_open_socket", return_value=fake_socket), mock.patch.object(
            webota_module.time, "sleep"
        ) as sleep_mock, mock.patch.object(webota_module, "_print_progress"):
            response = webota_module._send_raw(parsed, firmware, 7, headers, 4, 5.0, stats, 1.0, False)
            response.read()

    if len(fake_socket.writes) != 3:
        errors.append("scripts/esp32base_webota.py: raw upload must send first chunk, remaining body, and padding")
        return
    first, second, padding = fake_socket.writes
    if b"POST /esp32base/ota/raw HTTP/1.1\r\n" not in first:
        errors.append("scripts/esp32base_webota.py: raw upload must send a raw POST request")
    if b"Content-Length: 1436\r\n" not in first:
        errors.append("scripts/esp32base_webota.py: raw upload Content-Length must include padding")
    if not first.endswith(b"\r\n\r\nabcd"):
        errors.append("scripts/esp32base_webota.py: raw upload must send request headers and first firmware bytes together")
    if second != b"efg":
        errors.append("scripts/esp32base_webota.py: raw upload must stream remaining firmware bytes")
    if padding != b"\0" * 1429:
        errors.append("scripts/esp32base_webota.py: raw upload must send zero padding bytes")
    if sleep_mock.call_count != 2:
        errors.append("scripts/esp32base_webota.py: raw upload must sleep between paced raw chunks")


def main() -> int:
    webota_module = load_webota_module()
    webota = read("scripts/esp32base_webota.py")
    ota_docs = read("docs/05_ota.md")
    errors: list[str] = []

    require(
        webota_module.DEFAULT_UPLOAD_TIMEOUT_SEC == 90.0,
        "scripts/esp32base_webota.py: default upload timeout must be 90 seconds",
        errors,
    )
    if "`esp32base_webota_upload_timeout`：默认 `90` 秒" not in ota_docs:
        errors.append("docs/05_ota.md: webota upload timeout default must document 90 seconds")
    require(
        webota_module.DEFAULT_WEBOTA_PATH == "/esp32base/ota/raw",
        "scripts/esp32base_webota.py: default upload path must use raw /esp32base/ota/raw",
        errors,
    )
    require(
        webota_module.DEFAULT_RAW_CHUNK_SIZE == 64 * 1024,
        "scripts/esp32base_webota.py: default chunk size must be 65536 bytes",
        errors,
    )
    if "`esp32base_webota_path`：默认 `/esp32base/ota/raw`" not in ota_docs:
        errors.append("docs/05_ota.md: webota default raw path must be documented")
    if "`esp32base_webota_chunk_size`：默认 `65536` 字节" not in ota_docs:
        errors.append("docs/05_ota.md: webota chunk size default must document 65536 bytes")
    if "脚本默认 raw endpoint 使用 65536 字节分块" not in ota_docs:
        errors.append("docs/05_ota.md: raw Web OTA default chunk guidance must be documented")
    require(
        webota_module.HTTP_RAW_BUFLEN == 1436,
        "scripts/esp32base_webota.py: raw upload padding must use Arduino WebServer HTTP_RAW_BUFLEN",
        errors,
    )
    require(
        webota_module._raw_padded_size(1436) == 1436
        and webota_module._raw_padded_size(1437) == 2872,
        "scripts/esp32base_webota.py: raw upload must pad Content-Length to avoid final short-read timeout",
        errors,
    )
    weak_chunk, weak_pause, weak_note = webota_module._resolve_raw_transfer_settings(
        "raw",
        webota_module.DEFAULT_RAW_CHUNK_SIZE,
        webota_module.DEFAULT_RAW_PAUSE_MS,
        False,
        False,
        [{"rssi": -70}],
    )
    require(
        weak_chunk == webota_module.RAW_WEAK_CHUNK_SIZE
        and weak_pause == webota_module.RAW_WEAK_PAUSE_MS
        and weak_note
        and weak_note.get("level") == "weak",
        "scripts/esp32base_webota.py: raw upload must auto-adjust chunk size and pacing from weak RSSI",
        errors,
    )
    very_weak_chunk, very_weak_pause, very_weak_note = webota_module._resolve_raw_transfer_settings(
        "raw",
        webota_module.DEFAULT_RAW_CHUNK_SIZE,
        webota_module.DEFAULT_RAW_PAUSE_MS,
        False,
        False,
        [{"rssi": -75}],
    )
    require(
        very_weak_chunk == webota_module.RAW_VERY_WEAK_CHUNK_SIZE
        and very_weak_pause == webota_module.RAW_VERY_WEAK_PAUSE_MS
        and very_weak_note
        and very_weak_note.get("level") == "very-weak",
        "scripts/esp32base_webota.py: raw upload must auto-adjust chunk size and pacing from very weak RSSI",
        errors,
    )
    try:
        webota_module._validate_remote_ota_capacity({"nextUpdatePartition": {"size": 1024, "label": "ota_0"}}, 2048)
        errors.append("scripts/esp32base_webota.py: preflight must reject firmware larger than next OTA partition")
    except webota_module.WebOtaPreflightError as exc:
        if "Web OTA blocked before upload" not in str(exc):
            errors.append("scripts/esp32base_webota.py: oversize preflight error must clearly say upload was not sent")
    try:
        webota_module._validate_remote_ota_capacity({"nextUpdatePartition": {"size": 2048, "label": "ota_0"}}, 1024)
    except webota_module.WebOtaPreflightError:
        errors.append("scripts/esp32base_webota.py: preflight must allow firmware within next OTA partition")

    check_raw_upload_send_contract(webota_module, errors)
    if "connection.endheaders(first_chunk)" in webota:
        errors.append("scripts/esp32base_webota.py: raw upload must not rely on HTTPConnection.endheaders(first_chunk)")
    if "def _send_raw_aligned" in webota or "raw_send_carry" in webota:
        errors.append("scripts/esp32base_webota.py: raw upload must not split every socket send into HTTP_RAW_BUFLEN-sized writes")
    require(
        webota_module.RAW_RSSI_WEAK_DBM == -70 and webota_module.RAW_RSSI_VERY_WEAK_DBM == -75,
        "scripts/esp32base_webota.py: raw upload must define weak RSSI thresholds",
        errors,
    )
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
