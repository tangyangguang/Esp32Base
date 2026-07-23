import base64
import hashlib
import http.client
import json
import os
import socket
import ssl
import sys
import time
from pathlib import Path
from urllib.parse import urlparse

try:
    Import("env")
except NameError:
    env = None

HTTP_RAW_BUFLEN = 1436
DEFAULT_WEBOTA_PATH = "/esp32base/ota/raw"
DEFAULT_REQUEST_TIMEOUT_SEC = 120.0
DEFAULT_UPLOAD_TIMEOUT_SEC = 90.0
DEFAULT_RAW_CHUNK_SIZE = 64 * 1024
DEFAULT_RAW_PAUSE_MS = 0.0
RAW_RSSI_WEAK_DBM = -70
RAW_RSSI_VERY_WEAK_DBM = -75
RAW_WEAK_CHUNK_SIZE = 16 * 1024
RAW_VERY_WEAK_CHUNK_SIZE = 8 * 1024
RAW_WEAK_PAUSE_MS = 60.0
RAW_VERY_WEAK_PAUSE_MS = 120.0


class WebOtaPreflightError(RuntimeError):
    pass


def _config_get(section, key, default=None):
    config = env.GetProjectConfig()
    try:
        return config.get(section, key)
    except Exception:
        return default


def _option(name, default=None):
    value = _configured_option(name)
    return value if value not in (None, "") else default


def _configured_option(name):
    pioenv = env.subst("$PIOENV")
    env_section = "env:%s" % pioenv
    common_section = "esp32base_webota"
    value = _config_get(env_section, "custom_%s" % name)
    if value is None:
        value = _config_get(common_section, name)
    if value is None:
        value = os.environ.get(name.upper())
    return value


def _as_bool(value, default=False):
    if value is None:
        return default
    return str(value).strip().lower() in ("1", "true", "yes", "on")


def _as_int(value, default):
    if value in (None, ""):
        return default
    try:
        parsed = int(str(value).strip())
    except ValueError:
        raise ValueError("expected integer value, got: %s" % value)
    return parsed


def _as_float(value, default):
    if value in (None, ""):
        return default
    try:
        parsed = float(str(value).strip())
    except ValueError:
        raise ValueError("expected numeric value, got: %s" % value)
    if parsed <= 0:
        raise ValueError("timeout values must be greater than 0")
    return parsed


def _as_non_negative_float(value, default):
    if value in (None, ""):
        return default
    try:
        parsed = float(str(value).strip())
    except ValueError:
        raise ValueError("expected numeric value, got: %s" % value)
    if parsed < 0:
        raise ValueError("pacing values must be zero or greater")
    return parsed


def _format_bytes(value):
    value = int(value or 0)
    if value < 1024:
        return "%d B" % value
    if value < 1024 * 1024:
        return "%.1f KB" % (value / 1024.0)
    return "%.1f MB" % (value / 1024.0 / 1024.0)


def _format_duration(seconds):
    seconds = max(0.0, float(seconds or 0.0))
    if seconds < 60:
        return "%.2fs" % seconds
    minutes = int(seconds // 60)
    return "%dm %.2fs" % (minutes, seconds - minutes * 60)


def _format_timestamp(timestamp):
    return time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(timestamp))


def _format_speed(byte_count, seconds):
    seconds = float(seconds or 0.0)
    if seconds < 0.1:
        return "n/a"
    return "%s/s" % _format_bytes(byte_count / seconds)


def _upload_percent(sent_bytes, total_bytes):
    if not total_bytes:
        return 0
    return int(sent_bytes * 100 / total_bytes)


def _raw_padded_size(size):
    remainder = int(size) % HTTP_RAW_BUFLEN
    if remainder == 0:
        return int(size)
    return int(size) + (HTTP_RAW_BUFLEN - remainder)


def _network_upload_port():
    try:
        value = env.GetProjectOption("upload_port")
    except Exception:
        value = None
    if not value:
        return None
    value = str(value).strip()
    lower = value.lower()
    if lower.startswith(("/dev/", "com")):
        return None
    return value


def _build_url():
    url = _option("esp32base_webota_url")
    if url:
        parsed = urlparse(url)
        if not parsed.scheme or not parsed.netloc:
            raise ValueError("esp32base_webota_url must be an absolute http:// URL")
        return url

    host = _option("esp32base_webota_host") or _network_upload_port()
    if not host:
        raise ValueError(
            "missing OTA host: set esp32base_webota_host or esp32base_webota_url in platformio.ini"
        )
    port = _option("esp32base_webota_port", "80")
    path = _option("esp32base_webota_path", DEFAULT_WEBOTA_PATH)
    if not path.startswith("/"):
        path = "/" + path
    return "http://%s:%s%s" % (host, port, path)


def _resolve_target(parsed):
    host = parsed.hostname
    if not host:
        raise ValueError("Web OTA URL is missing a host")
    port = parsed.port or (443 if parsed.scheme == "https" else 80)
    results = socket.getaddrinfo(host, port, type=socket.SOCK_STREAM)
    addresses = []
    for result in results:
        sockaddr = result[4]
        if not sockaddr:
            continue
        address = str(sockaddr[0])
        if address not in addresses:
            addresses.append(address)
    if not addresses:
        raise socket.gaierror("no address returned for %s" % host)
    return addresses


def _print_resolution_failure(parsed, error):
    host = parsed.hostname or "<missing>"
    print("Error: Web OTA target resolution failed for %s: %s" % (host, error), file=sys.stderr)
    if host.lower().endswith(".local"):
        print(
            "Hint: .local uses mDNS. Check that the computer and device are on the same multicast-capable "
            "network, the device mDNS service is running, and the OS resolver supports mDNS.",
            file=sys.stderr,
        )
    else:
        print("Hint: check the configured IP address or DNS hostname.", file=sys.stderr)


def _create_resolved_socket(addresses, port, timeout, source_address=None):
    last_error = None
    for address in addresses:
        try:
            return socket.create_connection((address, port), timeout, source_address)
        except OSError as exc:
            last_error = exc
    if last_error is not None:
        raise last_error
    raise socket.gaierror("no resolved address available")


def _use_resolved_addresses(connection, addresses):
    if not addresses:
        return connection

    def create_connection(address, timeout=socket._GLOBAL_DEFAULT_TIMEOUT, source_address=None):
        return _create_resolved_socket(addresses, address[1], timeout, source_address)

    connection._create_connection = create_connection
    return connection


def _status_path(upload_path):
    configured = _option("esp32base_webota_status_path")
    if configured:
        return configured if configured.startswith("/") else "/" + configured
    if upload_path.rstrip("/") in ("/esp32base/ota", "/esp32base/ota/raw"):
        return "/esp32base/api/ota"
    return upload_path


def _request_target(parsed, path=None):
    target = path if path is not None else (parsed.path or "/")
    if parsed.query and path is None:
        target += "?" + parsed.query
    return target


def _open_connection(parsed, timeout, verify_tls, resolved_addresses=None):
    if parsed.scheme == "https":
        context = None if verify_tls else ssl._create_unverified_context()
        return _use_resolved_addresses(
            http.client.HTTPSConnection(
                parsed.hostname,
                parsed.port or 443,
                timeout=timeout,
                context=context,
            ),
            resolved_addresses,
        )
    if parsed.scheme == "http":
        return _use_resolved_addresses(
            http.client.HTTPConnection(parsed.hostname, parsed.port or 80, timeout=timeout),
            resolved_addresses,
        )
    raise ValueError("unsupported URL scheme: %s" % parsed.scheme)


def _open_socket(parsed, timeout, verify_tls, resolved_addresses=None):
    addresses = resolved_addresses or [parsed.hostname]
    if parsed.scheme == "http":
        sock = _create_resolved_socket(addresses, parsed.port or 80, timeout)
        _configure_raw_upload_socket(sock)
        return sock
    if parsed.scheme == "https":
        context = ssl.create_default_context() if verify_tls else ssl._create_unverified_context()
        raw_sock = _create_resolved_socket(addresses, parsed.port or 443, timeout)
        _configure_raw_upload_socket(raw_sock)
        try:
            return context.wrap_socket(raw_sock, server_hostname=parsed.hostname)
        except Exception:
            raw_sock.close()
            raise
    raise ValueError("unsupported URL scheme: %s" % parsed.scheme)


def _configure_raw_upload_socket(sock):
    try:
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    except OSError:
        pass
    send_buffer = _as_int(_option("esp32base_webota_socket_send_buffer"), 0)
    if send_buffer <= 0:
        return
    try:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, send_buffer)
    except OSError:
        pass


def _host_header(parsed):
    host = parsed.hostname or ""
    if ":" in host and not host.startswith("["):
        host = "[%s]" % host
    port = parsed.port
    if port and not ((parsed.scheme == "http" and port == 80) or (parsed.scheme == "https" and port == 443)):
        host = "%s:%d" % (host, port)
    return host


def _raw_request_bytes(parsed, headers):
    lines = [
        "POST %s HTTP/1.1" % _request_target(parsed),
        "Host: %s" % _host_header(parsed),
        "Accept-Encoding: identity",
    ]
    for key, value in headers.items():
        lines.append("%s: %s" % (key, value))
    lines.append("")
    lines.append("")
    return "\r\n".join(lines).encode("latin-1")


def _auth_header():
    auth = _option("esp32base_webota_auth")
    if auth:
        token = base64.b64encode(auth.encode("utf-8")).decode("ascii")
        return "Basic " + token
    user = _option("esp32base_webota_user")
    password = _option("esp32base_webota_password")
    if not user or not password:
        raise ValueError(
            "Web OTA auth is required: set esp32base_webota_auth or both "
            "esp32base_webota_user and esp32base_webota_password"
        )
    token = base64.b64encode(("%s:%s" % (user, password)).encode("utf-8")).decode("ascii")
    return "Basic " + token


def _sha256_header(firmware_path):
    mode = str(_option("esp32base_webota_sha256", "auto")).strip()
    if not mode or mode.lower() in ("0", "false", "off", "none", "no"):
        return None
    if mode.lower() == "auto":
        h = hashlib.sha256()
        with open(firmware_path, "rb") as fh:
            for chunk in iter(lambda: fh.read(1024 * 1024), b""):
                h.update(chunk)
        return h.hexdigest()
    if len(mode) == 64 and all(c in "0123456789abcdefABCDEF" for c in mode):
        return mode.lower()
    raise ValueError("esp32base_webota_sha256 must be auto, off, or a 64-char hex digest")


def _firmware_path(source):
    configured = _option("esp32base_webota_firmware")
    if configured:
        return Path(env.subst(configured)).expanduser()
    if source:
        return Path(str(source[0]))
    return Path(env.subst("$BUILD_DIR")) / ("%s.bin" % env.subst("$PROGNAME"))


def _read_error_body(response):
    data = response.read()
    text = data.decode("utf-8", "replace") if data else ""
    if not text:
        return ""
    try:
        payload = json.loads(text)
        if isinstance(payload, dict):
            return payload.get("error") or payload.get("message") or text
    except Exception:
        pass
    return text.strip()


def _json_bytes_value(value):
    if isinstance(value, dict):
        raw = value.get("bytes")
        return raw if isinstance(raw, int) else None
    return value if isinstance(value, int) else None


def _validate_remote_ota_capacity(payload, firmware_size):
    if not isinstance(payload, dict):
        return
    partition = payload.get("nextUpdatePartition")
    if not isinstance(partition, dict):
        return
    partition_size = _json_bytes_value(partition.get("size"))
    if not isinstance(partition_size, int) or partition_size <= 0:
        return
    if firmware_size <= partition_size:
        return
    label = partition.get("label") if isinstance(partition.get("label"), str) else "next OTA"
    raise WebOtaPreflightError(
        "Web OTA blocked before upload: firmware %s exceeds %s slot %s"
        % (_format_bytes(firmware_size), label, _format_bytes(partition_size))
    )


def _request_json(parsed, path, headers, timeout, verify_tls, resolved_addresses):
    conn = _open_connection(parsed, timeout, verify_tls, resolved_addresses)
    try:
        conn.request("GET", path, headers={"Authorization": headers["Authorization"]})
        response = conn.getresponse()
        data = response.read()
    finally:
        conn.close()
    if response.status < 200 or response.status >= 300:
        return None
    try:
        return json.loads(data.decode("utf-8", "replace") if data else "{}")
    except Exception:
        return None


def _sample_device(parsed, headers, timeout, verify_tls, resolved_addresses):
    status = _request_json(parsed, "/esp32base/api/status", headers, timeout, verify_tls, resolved_addresses) or {}
    ota = _request_json(
        parsed,
        _status_path(parsed.path or "/"),
        headers,
        timeout,
        verify_tls,
        resolved_addresses,
    ) or {}
    wifi = status.get("wifi") if isinstance(status, dict) else {}
    rssi = wifi.get("rssi") if isinstance(wifi, dict) else None
    if not isinstance(rssi, int):
        rssi = None
    return {
        "rssi": rssi,
        "otaElapsedMs": ota.get("elapsedMs") if isinstance(ota, dict) else None,
        "otaAverageBytesPerSecond": ota.get("averageBytesPerSecond") if isinstance(ota, dict) else None,
        "otaProgress": ota.get("progress") if isinstance(ota, dict) else None,
    }


def _sample_from_response(text):
    try:
        payload = json.loads(text) if text else {}
    except Exception:
        return {}
    if not isinstance(payload, dict):
        return {}
    return {
        "rssi": payload.get("rssi"),
        "otaElapsedMs": payload.get("elapsedMs"),
        "otaAverageBytesPerSecond": payload.get("averageBytesPerSecond"),
        "otaProgress": 100 if payload.get("ok") is True else payload.get("progress"),
    }


def _format_rssi(value):
    return "%d dBm" % value if isinstance(value, int) else "n/a"


def _format_device_sample(label, sample):
    parts = ["Web OTA device %s: rssi=%s" % (label, _format_rssi(sample.get("rssi")))]
    elapsed = sample.get("otaElapsedMs")
    avg = sample.get("otaAverageBytesPerSecond")
    progress = sample.get("otaProgress")
    if isinstance(progress, int):
        parts.append("otaProgress=%d%%" % progress)
    if isinstance(elapsed, int):
        parts.append("otaElapsed=%s" % _format_duration(elapsed / 1000.0))
    if isinstance(avg, int) and avg > 0:
        parts.append("deviceAvg=%s/s" % _format_bytes(avg))
    return ", ".join(parts)


def _sample_min_rssi(samples):
    values = [sample.get("rssi") for sample in samples if isinstance(sample.get("rssi"), int)]
    return min(values) if values else None


def _resolve_raw_transfer_settings(upload_mode, chunk_size, raw_pause_ms, chunk_size_configured, raw_pause_configured, samples):
    if upload_mode != "raw":
        return chunk_size, raw_pause_ms, None
    min_rssi = _sample_min_rssi(samples)
    if not isinstance(min_rssi, int):
        return chunk_size, raw_pause_ms, None

    target_chunk_size = None
    target_pause_ms = None
    level = None
    if min_rssi <= RAW_RSSI_VERY_WEAK_DBM:
        target_chunk_size = RAW_VERY_WEAK_CHUNK_SIZE
        target_pause_ms = RAW_VERY_WEAK_PAUSE_MS
        level = "very-weak"
    elif min_rssi <= RAW_RSSI_WEAK_DBM:
        target_chunk_size = RAW_WEAK_CHUNK_SIZE
        target_pause_ms = RAW_WEAK_PAUSE_MS
        level = "weak"

    if not level:
        return chunk_size, raw_pause_ms, None

    adjusted = []
    if not chunk_size_configured and chunk_size > target_chunk_size:
        chunk_size = target_chunk_size
        adjusted.append("chunk")
    if not raw_pause_configured and raw_pause_ms < target_pause_ms:
        raw_pause_ms = target_pause_ms
        adjusted.append("pause")
    note = {
        "level": level,
        "rssi": min_rssi,
        "adjusted": ",".join(adjusted) if adjusted else "none",
    }
    return chunk_size, raw_pause_ms, note


def _print_transfer_settings(upload_mode, chunk_size, raw_pause_ms, raw_note):
    print("Web OTA chunk size: %s (%d bytes)" % (_format_bytes(chunk_size), chunk_size))
    if upload_mode != "raw":
        return
    if raw_note:
        print(
            "Web OTA raw pacing: rssiMin=%s level=%s adjusted=%s pause=%.0f ms"
            % (_format_rssi(raw_note.get("rssi")), raw_note.get("level"), raw_note.get("adjusted"), raw_pause_ms)
        )
    elif raw_pause_ms > 0:
        print("Web OTA raw pacing: pause=%.0f ms" % raw_pause_ms)
    else:
        print("Web OTA raw pacing: off")


def _print_raw_failure_hint(upload_mode):
    if upload_mode != "raw":
        return
    print(
        "Hint: raw upload was interrupted. On weak links retry with "
        "set esp32base_webota_chunk_size, esp32base_webota_raw_pause_ms, "
        "or esp32base_webota_socket_send_buffer.",
        file=sys.stderr,
    )


def _print_failure_summary(stats, firmware_size, started_at):
    finished_at = time.time()
    sent = int(stats.get("sent_bytes", 0))
    percent = _upload_percent(sent, firmware_size)
    print("Web OTA failed: %s" % _format_timestamp(finished_at), file=sys.stderr)
    print(
        "Web OTA duration: %s, uploaded %s / %s (%d%%)"
        % (
            _format_duration(finished_at - started_at),
            _format_bytes(sent),
            _format_bytes(firmware_size),
            percent,
        ),
        file=sys.stderr,
    )


def _print_progress(percent, sent, total, elapsed):
    print(
        "Web OTA progress: pct=%3d%% sent=%s/%s elapsed=%s socketRate=%s"
        % (
            percent,
            _format_bytes(sent),
            _format_bytes(total),
            _format_duration(elapsed),
            _format_speed(sent, elapsed),
        )
    )


def _preflight(parsed, headers, timeout, verify_tls, firmware_size, resolved_addresses):
    if not _as_bool(_option("esp32base_webota_preflight", "true"), True):
        return
    conn = _open_connection(parsed, timeout, verify_tls, resolved_addresses)
    try:
        status_path = _status_path(parsed.path or "/")
        conn.request("GET", status_path, headers={"Authorization": headers["Authorization"]})
        response = conn.getresponse()
        data = response.read()
    finally:
        conn.close()
    text = data.decode("utf-8", "replace") if data else ""
    body_error = text.strip()
    payload = None
    if text:
        try:
            parsed_body = json.loads(text)
            if isinstance(parsed_body, dict):
                payload = parsed_body
                body_error = parsed_body.get("error") or parsed_body.get("message") or body_error
        except Exception:
            pass
    if response.status in (401, 403):
        raise PermissionError("HTTP %d %s" % (response.status, body_error))
    if response.status < 200 or response.status >= 300:
        raise RuntimeError("HTTP %d: %s" % (response.status, body_error))
    _validate_remote_ota_capacity(payload, firmware_size)


def _send_multipart(connection, parsed, firmware_path, firmware_size, headers, chunk_size, stats):
    boundary = "----esp32base-webota-%d" % int(time.time() * 1000)
    file_header = (
        "--%s\r\n"
        'Content-Disposition: form-data; name="firmware"; filename="%s"\r\n'
        "Content-Type: application/octet-stream\r\n\r\n"
    ) % (boundary, firmware_path.name)
    tail = "\r\n--%s--\r\n" % boundary
    file_header_bytes = file_header.encode("utf-8")
    tail_bytes = tail.encode("utf-8")
    content_length = len(file_header_bytes) + firmware_size + len(tail_bytes)

    request_headers = dict(headers)
    request_headers["Content-Type"] = "multipart/form-data; boundary=%s" % boundary
    request_headers["Content-Length"] = str(content_length)

    connection.putrequest("POST", _request_target(parsed))
    for key, value in request_headers.items():
        connection.putheader(key, value)
    connection.endheaders()
    connection.send(file_header_bytes)

    stats["sent_bytes"] = 0
    stats["upload_started_at"] = time.time()
    next_percent = 0
    with open(firmware_path, "rb") as fh:
        while True:
            chunk = fh.read(chunk_size)
            if not chunk:
                break
            connection.send(chunk)
            stats["sent_bytes"] += len(chunk)
            sent = stats["sent_bytes"]
            percent = _upload_percent(sent, firmware_size)
            if percent >= next_percent:
                elapsed = time.time() - stats["upload_started_at"]
                _print_progress(percent, sent, firmware_size, elapsed)
                stats["last_percent"] = percent
                next_percent += 5
    connection.send(tail_bytes)
    stats["client_send_finished_at"] = time.time()


def _send_raw(
    parsed,
    firmware_path,
    firmware_size,
    headers,
    chunk_size,
    raw_pause_ms,
    stats,
    timeout,
    verify_tls,
    resolved_addresses,
):
    padded_size = _raw_padded_size(firmware_size)
    padding_size = padded_size - firmware_size
    request_headers = dict(headers)
    request_headers["Content-Type"] = "application/octet-stream"
    request_headers["Content-Length"] = str(padded_size)
    request_head = _raw_request_bytes(parsed, request_headers)
    sock = _open_socket(parsed, timeout, verify_tls, resolved_addresses)

    stats["sent_bytes"] = 0
    stats["upload_started_at"] = time.time()
    next_percent = 0
    try:
        with open(firmware_path, "rb") as fh:
            first_chunk = fh.read(chunk_size)
            sock.sendall(request_head + first_chunk)
            if raw_pause_ms > 0:
                time.sleep(raw_pause_ms / 1000.0)
            stats["sent_bytes"] += len(first_chunk)
            sent = stats["sent_bytes"]
            percent = _upload_percent(sent, firmware_size)
            if percent >= next_percent:
                elapsed = time.time() - stats["upload_started_at"]
                _print_progress(percent, sent, firmware_size, elapsed)
                stats["last_percent"] = percent
                next_percent += 5
            while True:
                chunk = fh.read(chunk_size)
                if not chunk:
                    break
                sock.sendall(chunk)
                if raw_pause_ms > 0:
                    time.sleep(raw_pause_ms / 1000.0)
                stats["sent_bytes"] += len(chunk)
                sent = stats["sent_bytes"]
                percent = _upload_percent(sent, firmware_size)
                if percent >= next_percent:
                    elapsed = time.time() - stats["upload_started_at"]
                    _print_progress(percent, sent, firmware_size, elapsed)
                    stats["last_percent"] = percent
                    next_percent += 5
        if padding_size:
            sock.sendall(b"\0" * padding_size)
            stats["raw_padding_bytes"] = padding_size
        stats["client_send_finished_at"] = time.time()
        stats["response_wait_started_at"] = time.time()
        response = http.client.HTTPResponse(sock)
        response.begin()
        return response
    except Exception:
        sock.close()
        raise


def _upload_once(
    upload_mode,
    parsed,
    firmware,
    firmware_size,
    headers,
    chunk_size,
    raw_pause_ms,
    stats,
    upload_timeout,
    verify_tls,
    resolved_addresses,
):
    if upload_mode == "multipart":
        conn = _open_connection(parsed, upload_timeout, verify_tls, resolved_addresses)
        try:
            _send_multipart(conn, parsed, firmware, firmware_size, headers, chunk_size, stats)
            stats["response_wait_started_at"] = time.time()
            response = conn.getresponse()
            stats["response_received_at"] = time.time()
            return response
        finally:
            conn.close()
    return _send_raw(
        parsed,
        firmware,
        firmware_size,
        headers,
        chunk_size,
        raw_pause_ms,
        stats,
        upload_timeout,
        verify_tls,
        resolved_addresses,
    )


def _run_webota(target, source, env):
    started_at = time.time()
    stats = {"sent_bytes": 0, "upload_started_at": None, "last_percent": 0}
    firmware = _firmware_path(source)
    if not firmware.exists():
        print("Error: firmware not found: %s" % firmware, file=sys.stderr)
        env.Exit(1)
    firmware_size = firmware.stat().st_size
    if firmware_size <= 0:
        print("Error: firmware is empty: %s" % firmware, file=sys.stderr)
        env.Exit(1)

    try:
        url = _build_url()
        parsed = urlparse(url)
        sha256 = _sha256_header(firmware)
        auth_header = _auth_header()
    except ValueError as exc:
        print("Error: %s" % exc, file=sys.stderr)
        env.Exit(1)

    headers = {
        "Authorization": auth_header,
        "X-Firmware-Size": str(firmware_size),
    }
    if sha256:
        headers["X-Sha256"] = sha256

    try:
        timeout = _as_float(_option("esp32base_webota_timeout"), DEFAULT_REQUEST_TIMEOUT_SEC)
        upload_timeout = _as_float(_option("esp32base_webota_upload_timeout"), DEFAULT_UPLOAD_TIMEOUT_SEC)
    except ValueError as exc:
        print("Error: %s" % exc, file=sys.stderr)
        env.Exit(1)
    verify_tls = _as_bool(_option("esp32base_webota_verify_tls"), False)
    chunk_size_configured = _configured_option("esp32base_webota_chunk_size") not in (None, "")
    raw_pause_configured = _configured_option("esp32base_webota_raw_pause_ms") not in (None, "")
    try:
        chunk_size = _as_int(_option("esp32base_webota_chunk_size"), DEFAULT_RAW_CHUNK_SIZE)
        raw_pause_ms = _as_non_negative_float(_option("esp32base_webota_raw_pause_ms"), DEFAULT_RAW_PAUSE_MS)
    except ValueError as exc:
        print("Error: %s" % exc, file=sys.stderr)
        env.Exit(1)
    if chunk_size < 4096:
        print("Error: esp32base_webota_chunk_size must be at least 4096", file=sys.stderr)
        env.Exit(1)
    print("Web OTA started: %s" % _format_timestamp(started_at))
    print("Web OTA target: %s" % url)
    print("Web OTA firmware: %s (%s, %d bytes)" % (firmware, _format_bytes(firmware_size), firmware_size))
    print("Web OTA timeouts: request %.1fs, upload %.1fs" % (timeout, upload_timeout))
    upload_path = (parsed.path or "/").rstrip("/")
    upload_mode = "multipart" if upload_path == "/esp32base/ota" else "raw"
    print("Web OTA mode: %s" % upload_mode)

    try:
        resolution_started_at = time.time()
        resolved_addresses = _resolve_target(parsed)
        print(
            "Web OTA resolved target: %s -> %s (%s)"
            % (
                parsed.hostname,
                ", ".join(resolved_addresses),
                _format_duration(time.time() - resolution_started_at),
            )
        )
        _preflight(parsed, headers, timeout, verify_tls, firmware_size, resolved_addresses)
        samples_before = []
        for sample_index in range(3):
            sample_before = _sample_device(parsed, headers, timeout, verify_tls, resolved_addresses)
            samples_before.append(sample_before)
            print(_format_device_sample("before-send %d/3" % (sample_index + 1), sample_before))
            if sample_index < 2:
                time.sleep(0.25)
        chunk_size, raw_pause_ms, raw_note = _resolve_raw_transfer_settings(
            upload_mode,
            chunk_size,
            raw_pause_ms,
            chunk_size_configured,
            raw_pause_configured,
            samples_before,
        )
        _print_transfer_settings(upload_mode, chunk_size, raw_pause_ms, raw_note)
        response = _upload_once(
            upload_mode,
            parsed,
            firmware,
            firmware_size,
            headers,
            chunk_size,
            raw_pause_ms,
            stats,
            upload_timeout,
            verify_tls,
            resolved_addresses,
        )
        stats["response_received_at"] = time.time()
    except PermissionError as exc:
        print("Error: authentication failed: %s" % exc, file=sys.stderr)
        _print_failure_summary(stats, firmware_size, started_at)
        env.Exit(1)
    except WebOtaPreflightError as exc:
        print("Error: %s" % exc, file=sys.stderr)
        _print_failure_summary(stats, firmware_size, started_at)
        env.Exit(1)
    except RuntimeError as exc:
        print("Error: device returned %s" % exc, file=sys.stderr)
        _print_failure_summary(stats, firmware_size, started_at)
        env.Exit(1)
    except (BrokenPipeError, ConnectionResetError, http.client.HTTPException) as exc:
        print("Error: upload interrupted: %s" % exc, file=sys.stderr)
        _print_raw_failure_hint(upload_mode)
        _print_failure_summary(stats, firmware_size, started_at)
        env.Exit(1)
    except socket.gaierror as exc:
        _print_resolution_failure(parsed, exc)
        _print_failure_summary(stats, firmware_size, started_at)
        env.Exit(1)
    except (ConnectionRefusedError, TimeoutError, socket.timeout, OSError) as exc:
        print("Error: connection failed: %s" % exc, file=sys.stderr)
        _print_raw_failure_hint(upload_mode)
        _print_failure_summary(stats, firmware_size, started_at)
        env.Exit(1)
    except ValueError as exc:
        print("Error: %s" % exc, file=sys.stderr)
        _print_failure_summary(stats, firmware_size, started_at)
        env.Exit(1)

    body_error = _read_error_body(response)
    if response.status in (401, 403):
        print("Error: authentication failed: HTTP %d %s" % (response.status, body_error), file=sys.stderr)
        _print_failure_summary(stats, firmware_size, started_at)
        env.Exit(1)
    if response.status < 200 or response.status >= 300:
        print("Error: device returned HTTP %d: %s" % (response.status, body_error), file=sys.stderr)
        _print_failure_summary(stats, firmware_size, started_at)
        env.Exit(1)

    finished_at = time.time()
    duration = finished_at - started_at
    send_started = stats.get("upload_started_at") or started_at
    send_finished = stats.get("client_send_finished_at") or finished_at
    response_started = stats.get("response_wait_started_at") or send_finished
    response_received = stats.get("response_received_at") or finished_at
    client_send_duration = max(0.0, send_finished - send_started)
    response_wait_duration = max(0.0, response_received - response_started)
    sample_after_response = _sample_from_response(body_error)
    print("Web OTA success: device accepted firmware and is restarting")
    print("Web OTA finished: %s" % _format_timestamp(finished_at))
    print(_format_device_sample("after-response", sample_after_response))
    print(
        "Web OTA client send: %s, %s"
        % (_format_duration(client_send_duration), _format_speed(stats["sent_bytes"], client_send_duration))
    )
    print("Web OTA wait response: %s" % _format_duration(response_wait_duration))
    print(
        "Web OTA duration: %s, uploaded %s, end-to-end average %s"
        % (_format_duration(duration), _format_bytes(stats["sent_bytes"]), _format_speed(stats["sent_bytes"], duration))
    )


if env is not None:
    firmware_target = os.path.join(env.subst("$BUILD_DIR"), "%s.bin" % env.subst("$PROGNAME"))
    webota_action = env.Action(_run_webota, "Uploading via Esp32Base Web OTA")
    webota_target = env.AddCustomTarget(
        "webota",
        firmware_target,
        webota_action,
        title="Esp32Base Web OTA",
        description="Upload firmware through Esp32Base HTTP Web OTA",
    )
    env.AlwaysBuild(webota_target)
