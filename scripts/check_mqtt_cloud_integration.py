#!/usr/bin/env python3
"""Run credential-safe MQTT 3.1.1 integration checks against a TLS broker."""

from __future__ import annotations

import argparse
import secrets
import socket
import ssl
import sys
from configparser import RawConfigParser
from dataclasses import dataclass
from pathlib import Path


def mqtt_string(value: str | bytes) -> bytes:
    data = value.encode("utf-8") if isinstance(value, str) else value
    if len(data) > 0xFFFF:
        raise ValueError("MQTT string exceeds 65535 bytes")
    return len(data).to_bytes(2, "big") + data


def remaining_length(value: int) -> bytes:
    encoded = bytearray()
    while True:
        digit = value % 128
        value //= 128
        if value:
            digit |= 0x80
        encoded.append(digit)
        if not value:
            return bytes(encoded)


def recv_exact(sock: ssl.SSLSocket, length: int) -> bytes:
    result = bytearray()
    while len(result) < length:
        chunk = sock.recv(length - len(result))
        if not chunk:
            raise ConnectionError("broker closed the connection")
        result.extend(chunk)
    return bytes(result)


@dataclass(frozen=True)
class Packet:
    packet_type: int
    flags: int
    body: bytes


class MqttClient:
    def __init__(
        self,
        host: str,
        port: int,
        ca_file: Path,
        client_id: str,
        username: str,
        password: str,
        *,
        will_topic: str | None = None,
        will_payload: bytes = b"",
        will_qos: int = 0,
        will_retain: bool = False,
    ) -> None:
        self.host = host
        self.port = port
        self.ca_file = ca_file
        self.client_id = client_id
        self.username = username
        self.password = password
        self.will_topic = will_topic
        self.will_payload = will_payload
        self.will_qos = will_qos
        self.will_retain = will_retain
        self.sock: ssl.SSLSocket | None = None
        self.next_packet_id = 1

    def connect(self) -> int:
        context = ssl.create_default_context(cafile=str(self.ca_file))
        context.check_hostname = True
        context.verify_mode = ssl.CERT_REQUIRED
        tcp = socket.create_connection((self.host, self.port), timeout=10)
        try:
            self.sock = context.wrap_socket(tcp, server_hostname=self.host)
        except Exception:
            tcp.close()
            raise
        self.sock.settimeout(10)

        flags = 0xC2  # username, password, clean session
        payload = mqtt_string(self.client_id)
        if self.will_topic is not None:
            if self.will_qos not in (0, 1):
                raise ValueError("only QoS 0/1 wills are supported by this check")
            flags |= 0x04 | (self.will_qos << 3)
            if self.will_retain:
                flags |= 0x20
            payload += mqtt_string(self.will_topic)
            payload += mqtt_string(self.will_payload)
        payload += mqtt_string(self.username)
        payload += mqtt_string(self.password)
        variable = b"\x00\x04MQTT" + bytes([4, flags]) + (60).to_bytes(2, "big")
        self._send(0x10, variable + payload)
        packet = self.read_packet()
        if packet.packet_type != 2 or len(packet.body) != 2:
            raise RuntimeError("broker returned a malformed CONNACK")
        return packet.body[1]

    def disconnect(self) -> None:
        if self.sock is None:
            return
        try:
            self._send(0xE0, b"")
        finally:
            self.sock.close()
            self.sock = None

    def close_without_disconnect(self) -> None:
        if self.sock is not None:
            self.sock.close()
            self.sock = None

    def subscribe(self, topic: str, qos: int) -> int:
        packet_id = self._packet_id()
        body = packet_id.to_bytes(2, "big") + mqtt_string(topic) + bytes([qos])
        self._send(0x82, body)
        return packet_id

    def publish(self, topic: str, payload: bytes, qos: int, retain: bool) -> int:
        packet_id = self._packet_id() if qos == 1 else 0
        body = mqtt_string(topic)
        if qos == 1:
            body += packet_id.to_bytes(2, "big")
        body += payload
        header = 0x30 | (qos << 1) | (0x01 if retain else 0)
        self._send(header, body)
        return packet_id

    def read_packet(self) -> Packet:
        if self.sock is None:
            raise RuntimeError("MQTT client is not connected")
        first = recv_exact(self.sock, 1)[0]
        length = 0
        multiplier = 1
        for _ in range(4):
            digit = recv_exact(self.sock, 1)[0]
            length += (digit & 0x7F) * multiplier
            if not digit & 0x80:
                return Packet(first >> 4, first & 0x0F, recv_exact(self.sock, length))
            multiplier *= 128
        raise RuntimeError("broker returned a malformed remaining length")

    def _packet_id(self) -> int:
        value = self.next_packet_id
        self.next_packet_id = 1 if value == 0xFFFF else value + 1
        return value

    def _send(self, header: int, body: bytes) -> None:
        if self.sock is None:
            raise RuntimeError("MQTT client is not connected")
        self.sock.sendall(bytes([header]) + remaining_length(len(body)) + body)


def parse_publish(packet: Packet) -> tuple[str, int, bytes, bool]:
    if packet.packet_type != 3 or len(packet.body) < 2:
        raise RuntimeError("expected an MQTT PUBLISH packet")
    topic_length = int.from_bytes(packet.body[:2], "big")
    offset = 2
    if topic_length == 0 or offset + topic_length > len(packet.body):
        raise RuntimeError("broker returned a malformed PUBLISH topic")
    topic = packet.body[offset : offset + topic_length].decode("utf-8")
    offset += topic_length
    qos = (packet.flags >> 1) & 0x03
    packet_id = 0
    if qos > 0:
        if offset + 2 > len(packet.body):
            raise RuntimeError("broker returned a malformed PUBLISH packet id")
        packet_id = int.from_bytes(packet.body[offset : offset + 2], "big")
        offset += 2
    return topic, packet_id, packet.body[offset:], bool(packet.flags & 0x01)


def wait_for_suback(client: MqttClient, packet_id: int) -> None:
    packet = client.read_packet()
    if (
        packet.packet_type != 9
        or len(packet.body) < 3
        or int.from_bytes(packet.body[:2], "big") != packet_id
        or packet.body[2] >= 0x80
    ):
        raise RuntimeError("subscription was not acknowledged")


def wait_for_puback_and_message(
    client: MqttClient,
    publish_packet_id: int,
    topic: str,
    payload: bytes,
) -> None:
    acknowledged = False
    delivered = False
    for _ in range(4):
        packet = client.read_packet()
        if packet.packet_type == 4 and len(packet.body) == 2:
            acknowledged |= int.from_bytes(packet.body, "big") == publish_packet_id
        elif packet.packet_type == 3:
            received_topic, incoming_id, received_payload, _ = parse_publish(packet)
            if incoming_id:
                client._send(0x40, incoming_id.to_bytes(2, "big"))
            delivered |= received_topic == topic and received_payload == payload
        if acknowledged and delivered:
            return
    raise RuntimeError("QoS 1 publish did not produce both PUBACK and subscribed delivery")


def wait_for_message(
    client: MqttClient,
    topic: str,
    payload: bytes,
    *,
    require_retain: bool,
) -> None:
    for _ in range(4):
        packet = client.read_packet()
        if packet.packet_type != 3:
            continue
        received_topic, incoming_id, received_payload, retained = parse_publish(packet)
        if incoming_id:
            client._send(0x40, incoming_id.to_bytes(2, "big"))
        if (
            received_topic == topic
            and received_payload == payload
            and (retained or not require_retain)
        ):
            return
    raise RuntimeError("expected subscribed message was not received")


def load_config(path: Path) -> tuple[dict[str, str], dict[str, str]]:
    parser = RawConfigParser(interpolation=None)
    if not parser.read(path):
        raise ValueError(f"cannot read config file: {path}")
    connection = dict(parser["connection"])
    topics = dict(parser["topics"])
    required_connection = (
        "host",
        "port",
        "client_id",
        "username",
        "password",
        "ca_file",
    )
    required_topics = ("prefix",)
    if any(not connection.get(name, "").strip() for name in required_connection):
        raise ValueError("local MQTT connection config is incomplete")
    if any(not topics.get(name, "").strip() for name in required_topics):
        raise ValueError("local MQTT topic config is incomplete")
    return connection, topics


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--config",
        default="local_private/emqx_mqtt.ini",
        help="ignored local INI containing broker credentials",
    )
    args = parser.parse_args()
    config_path = Path(args.config)
    connection, topics = load_config(config_path)
    ca_file = config_path.parent / connection["ca_file"].strip()
    if not ca_file.is_file():
        raise ValueError("configured CA file does not exist")

    host = connection["host"].strip()
    port = int(connection["port"])
    base_client_id = connection["client_id"].strip()
    username = connection["username"]
    password = connection["password"]
    suffix = secrets.token_hex(6)
    test_prefix = f"{topics['prefix'].rstrip('/')}/integration/{suffix}"
    echo_topic = f"{test_prefix}/echo"
    retained_topic = f"{test_prefix}/retained"
    will_topic = f"{test_prefix}/will"

    correct = MqttClient(
        host, port, ca_file, f"{base_client_id}-{suffix}-main", username, password
    )
    if correct.connect() != 0:
        raise RuntimeError("valid credentials were rejected")
    print("PASS tls_hostname_sni_and_authentication")

    rejected = MqttClient(
        host,
        port,
        ca_file,
        f"{base_client_id}-{suffix}-negative",
        username,
        password + "-deliberately-invalid",
    )
    reject_code = rejected.connect()
    rejected.close_without_disconnect()
    if reject_code not in (4, 5):
        raise RuntimeError("invalid password was not rejected")
    print("PASS invalid_password_rejected")

    subscription_id = correct.subscribe(echo_topic, 1)
    wait_for_suback(correct, subscription_id)
    qos0_payload = b"qos0-" + suffix.encode()
    correct.publish(echo_topic, qos0_payload, 0, False)
    wait_for_message(correct, echo_topic, qos0_payload, require_retain=False)
    print("PASS subscribe_and_qos0")

    qos1_payload = b"qos1-" + suffix.encode()
    qos1_id = correct.publish(echo_topic, qos1_payload, 1, False)
    wait_for_puback_and_message(correct, qos1_id, echo_topic, qos1_payload)
    print("PASS qos1_puback_and_delivery")

    retained_payload = b"retained-" + suffix.encode()
    retained_id = correct.publish(retained_topic, retained_payload, 1, True)
    packet = correct.read_packet()
    if packet.packet_type != 4 or int.from_bytes(packet.body, "big") != retained_id:
        raise RuntimeError("retained publish was not acknowledged")
    retained_reader = MqttClient(
        host,
        port,
        ca_file,
        f"{base_client_id}-{suffix}-retained",
        username,
        password,
    )
    if retained_reader.connect() != 0:
        raise RuntimeError("retained reader credentials were rejected")
    retained_sub_id = retained_reader.subscribe(retained_topic, 1)
    wait_for_suback(retained_reader, retained_sub_id)
    wait_for_message(
        retained_reader, retained_topic, retained_payload, require_retain=True
    )
    retained_reader.disconnect()
    cleanup_id = correct.publish(retained_topic, b"", 1, True)
    cleanup_ack = correct.read_packet()
    if (
        cleanup_ack.packet_type != 4
        or int.from_bytes(cleanup_ack.body, "big") != cleanup_id
    ):
        raise RuntimeError("retained cleanup was not acknowledged")
    print("PASS retained_publish_and_cleanup")

    observer = MqttClient(
        host,
        port,
        ca_file,
        f"{base_client_id}-{suffix}-observer",
        username,
        password,
    )
    if observer.connect() != 0:
        raise RuntimeError("will observer credentials were rejected")
    will_sub_id = observer.subscribe(will_topic, 1)
    wait_for_suback(observer, will_sub_id)
    will_payload = b"offline-" + suffix.encode()
    will_client = MqttClient(
        host,
        port,
        ca_file,
        f"{base_client_id}-{suffix}-will",
        username,
        password,
        will_topic=will_topic,
        will_payload=will_payload,
        will_qos=1,
        will_retain=True,
    )
    if will_client.connect() != 0:
        raise RuntimeError("will client credentials were rejected")
    will_client.close_without_disconnect()
    wait_for_message(observer, will_topic, will_payload, require_retain=False)
    will_cleanup_id = observer.publish(will_topic, b"", 1, True)
    for _ in range(4):
        packet = observer.read_packet()
        if (
            packet.packet_type == 4
            and int.from_bytes(packet.body, "big") == will_cleanup_id
        ):
            break
        if packet.packet_type == 3:
            _, incoming_id, _, _ = parse_publish(packet)
            if incoming_id:
                observer._send(0x40, incoming_id.to_bytes(2, "big"))
    else:
        raise RuntimeError("will retain cleanup was not acknowledged")
    observer.disconnect()
    correct.disconnect()
    print("PASS last_will_and_cleanup")
    print("MQTT cloud integration checks passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ConnectionError, OSError, RuntimeError, ValueError, ssl.SSLError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
