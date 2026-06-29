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
    errors: list[str] = []

    require(
        webota_module.DEFAULT_UPLOAD_TIMEOUT_SEC == 90.0,
        "scripts/esp32base_webota.py: default upload timeout must be 90 seconds",
        errors,
    )
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

    with mock.patch.object(webota_module, "_option", return_value=None):
        try:
            webota_module._auth_header()
            errors.append("scripts/esp32base_webota.py: Web OTA must reject missing auth configuration")
        except ValueError:
            pass

    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1
    print("Web OTA default checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
