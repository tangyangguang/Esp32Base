#!/usr/bin/env python3
import io
import importlib.util
import tempfile
from pathlib import Path
from unittest import mock
from urllib.parse import urlparse


ROOT = Path(__file__).resolve().parents[1]
WEBOTA_PATH = ROOT / "scripts" / "esp32base_webota.py"
WEBOTA_HANDLER_PATH = ROOT / "src" / "web" / "internal" / "WebOta.cpp"


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
        self.options: list[tuple[int, int, int]] = []
        self.closed = False

    def sendall(self, data: bytes) -> None:
        self.writes.append(data)

    def setsockopt(self, level: int, option: int, value: int) -> None:
        self.options.append((level, option, value))

    def makefile(self, *args, **kwargs):
        return io.BytesIO(b"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK")

    def close(self) -> None:
        self.closed = True


def check_raw_upload_send_contract(webota_module, errors: list[str]) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        firmware = Path(tmp) / "firmware.bin"
        firmware.write_bytes(b"abcdefg")
        firmware_size = 7
        padded_size = webota_module._raw_padded_size(firmware_size)
        padding_size = padded_size - firmware_size
        fake_socket = FakeRawSocket()
        stats: dict[str, object] = {}
        parsed = urlparse("http://esp32.local/esp32base/ota/raw")
        headers = {
            "Authorization": "Basic dGVzdDp0ZXN0",
            "X-Firmware-Size": str(firmware_size),
        }
        with mock.patch.object(webota_module, "_open_socket", return_value=fake_socket), mock.patch.object(
            webota_module.time, "sleep"
        ) as sleep_mock, mock.patch.object(webota_module, "_print_progress"):
            response = webota_module._send_raw(
                parsed,
                firmware,
                firmware_size,
                headers,
                4,
                5.0,
                stats,
                1.0,
                False,
                ["192.168.2.112"],
            )
            response.read()

    if len(fake_socket.writes) != 3:
        errors.append("scripts/esp32base_webota.py: raw upload must send first chunk, remaining body, and padding")
        return
    first, second, padding = fake_socket.writes
    if b"POST /esp32base/ota/raw HTTP/1.1\r\n" not in first:
        errors.append("scripts/esp32base_webota.py: raw upload must send a raw POST request")
    expected_length = f"Content-Length: {padded_size}\r\n".encode("ascii")
    if expected_length not in first:
        errors.append("scripts/esp32base_webota.py: raw upload Content-Length must include padding")
    if not first.endswith(b"\r\n\r\nabcd"):
        errors.append("scripts/esp32base_webota.py: raw upload must send request headers and first firmware bytes together")
    if second != b"efg":
        errors.append("scripts/esp32base_webota.py: raw upload must stream remaining firmware bytes")
    if padding != b"\0" * padding_size:
        errors.append("scripts/esp32base_webota.py: raw upload must send zero padding bytes")
    if sleep_mock.call_count != 2:
        errors.append("scripts/esp32base_webota.py: raw upload must sleep between paced raw chunks")


def check_raw_socket_options(webota_module, errors: list[str]) -> None:
    fake_socket = FakeRawSocket()

    def option(name: str, default=None):
        if name == "esp32base_webota_socket_send_buffer":
            return "4096"
        return default

    with mock.patch.object(webota_module, "_option", side_effect=option):
        webota_module._configure_raw_upload_socket(fake_socket)

    socket_module = webota_module.socket
    expected = {
        (socket_module.IPPROTO_TCP, socket_module.TCP_NODELAY, 1),
        (socket_module.SOL_SOCKET, socket_module.SO_SNDBUF, 4096),
    }
    if not expected.issubset(set(fake_socket.options)):
        errors.append("scripts/esp32base_webota.py: raw upload socket must set TCP_NODELAY and configurable SO_SNDBUF")


def check_host_configuration_contract(webota_module, errors: list[str]) -> None:
    cases = {
        "192.168.2.112": "http://192.168.2.112:80/esp32base/ota/raw",
        "esp32base-full.local": "http://esp32base-full.local:80/esp32base/ota/raw",
        "device.example.lan": "http://device.example.lan:80/esp32base/ota/raw",
    }
    for configured_host, expected_url in cases.items():
        def option(name: str, default=None):
            if name == "esp32base_webota_host":
                return configured_host
            return default

        with mock.patch.object(webota_module, "_option", side_effect=option):
            actual_url = webota_module._build_url()
        if actual_url != expected_url:
            errors.append(
                "scripts/esp32base_webota.py: esp32base_webota_host must accept IP, DNS hostname, and mDNS .local"
            )
            break

    addrinfo = [
        (2, 1, 6, "", ("192.168.2.112", 80)),
        (2, 1, 6, "", ("192.168.2.112", 80)),
    ]
    parsed = urlparse("http://esp32base-full.local/esp32base/ota/raw")
    with mock.patch.object(webota_module.socket, "getaddrinfo", return_value=addrinfo) as resolver:
        addresses = webota_module._resolve_target(parsed)
    if addresses != ["192.168.2.112"]:
        errors.append("scripts/esp32base_webota.py: target resolution must return unique resolved addresses")
    resolver.assert_called_once_with("esp32base-full.local", 80, type=webota_module.socket.SOCK_STREAM)

    fake_socket = FakeRawSocket()
    connection = webota_module._open_connection(parsed, 1.0, False, addresses)
    with mock.patch.object(webota_module.socket, "create_connection", return_value=fake_socket) as connector:
        connected_socket = connection._create_connection((parsed.hostname, 80), 1.0, None)
    if connected_socket is not fake_socket or connection.host != "esp32base-full.local":
        errors.append(
            "scripts/esp32base_webota.py: HTTP connections must reuse the resolved IP while preserving the hostname"
        )
    connector.assert_called_once_with(("192.168.2.112", 80), 1.0, None)


def check_raw_device_write_contract(errors: list[str]) -> None:
    handler = WEBOTA_HANDLER_PATH.read_text(encoding="utf-8")
    if "Esp32BaseOta::writeChunk(raw.buf, writeLen)" not in handler:
        errors.append("src/web/internal/WebOta.cpp: raw handler must stream HTTPRaw chunks directly to the OTA layer")
    if "raw ota buffer allocation failed" in handler or "kRawOtaWriteBufferSize" in handler:
        errors.append("src/web/internal/WebOta.cpp: raw handler must not require a separate large aggregation buffer")


def main() -> int:
    webota_module = load_webota_module()
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
    raw_boundary = webota_module.HTTP_RAW_BUFLEN
    require(
        isinstance(raw_boundary, int) and raw_boundary > 0,
        "scripts/esp32base_webota.py: raw upload padding boundary must be a positive integer",
        errors,
    )
    require(
        webota_module._raw_padded_size(raw_boundary) == raw_boundary
        and webota_module._raw_padded_size(raw_boundary + 1) == raw_boundary * 2,
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
    except webota_module.WebOtaPreflightError:
        pass
    try:
        webota_module._validate_remote_ota_capacity({"nextUpdatePartition": {"size": 2048, "label": "ota_0"}}, 1024)
    except webota_module.WebOtaPreflightError:
        errors.append("scripts/esp32base_webota.py: preflight must allow firmware within next OTA partition")

    check_raw_upload_send_contract(webota_module, errors)
    check_raw_socket_options(webota_module, errors)
    check_host_configuration_contract(webota_module, errors)
    check_raw_device_write_contract(errors)
    require(
        webota_module.RAW_RSSI_WEAK_DBM == -70 and webota_module.RAW_RSSI_VERY_WEAK_DBM == -75,
        "scripts/esp32base_webota.py: raw upload must define weak RSSI thresholds",
        errors,
    )

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
