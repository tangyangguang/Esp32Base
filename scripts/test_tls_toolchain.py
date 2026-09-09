#!/usr/bin/env python3
"""Failure-path tests for the controlled toolchain's configuration audit."""

from contextlib import redirect_stdout
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import build_tls_toolchain as toolchain


class ToolchainAuditTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        root = Path(self.temporary.name)
        self.baseline = root / "baseline/esp32"
        self.output = root / "builder/out/tools/esp32-arduino-libs/esp32"
        self.baseline.mkdir(parents=True)
        self.output.mkdir(parents=True)
        self.before = "CONFIG_BUFFER_SIZE=4096\n# CONFIG_MBEDTLS_HAVE_TIME_DATE is not set\n# CONFIG_MQTT_TASK_CORE_SELECTION_ENABLED is not set\n"
        self.after = "CONFIG_BUFFER_SIZE=4096\nCONFIG_MBEDTLS_HAVE_TIME_DATE=y\nCONFIG_MQTT_TASK_CORE_SELECTION_ENABLED=y\nCONFIG_MQTT_USE_CORE_0=y\n# CONFIG_MQTT_USE_CORE_1 is not set\n"
        self.before += """CONFIG_ESP_HTTPS_SERVER_ENABLE=y
CONFIG_MBEDTLS_TLS_CLIENT=y
CONFIG_MBEDTLS_TLS_SERVER_AND_CLIENT=y
# CONFIG_MBEDTLS_TLS_CLIENT_ONLY is not set
CONFIG_MBEDTLS_TLS_SERVER=y
CONFIG_MBEDTLS_SERVER_SSL_SESSION_TICKETS=y
CONFIG_MQTT_TRANSPORT_WEBSOCKET=y
CONFIG_MQTT_TRANSPORT_WEBSOCKET_SECURE=y
"""
        self.after += """# CONFIG_ESP_HTTPS_SERVER_ENABLE is not set
CONFIG_MBEDTLS_TLS_CLIENT=y
# CONFIG_MBEDTLS_TLS_SERVER_AND_CLIENT is not set
CONFIG_MBEDTLS_TLS_CLIENT_ONLY=y
# CONFIG_MBEDTLS_TLS_SERVER is not set
# CONFIG_MBEDTLS_SERVER_SSL_SESSION_TICKETS is not set
# CONFIG_MQTT_TRANSPORT_WEBSOCKET is not set
# CONFIG_MQTT_TRANSPORT_WEBSOCKET_SECURE is not set
"""
        (self.baseline / "sdkconfig").write_text(self.before)
        (self.output / "sdkconfig").write_text(self.after)
        for directory in (self.baseline, self.output):
            (directory / "dependencies.lock").write_text("dependency: pinned\n")
        for name, value in (("BASELINE", root / "baseline"), ("BUILDER", root / "builder")):
            patcher = patch.object(toolchain, name, value)
            patcher.start()
            self.addCleanup(patcher.stop)

    def audit(self):
        with redirect_stdout(io.StringIO()):
            toolchain.audit("esp32")

    def test_accepts_only_intended_configuration_change(self):
        self.audit()

    def test_rejects_dates_still_disabled(self):
        (self.output / "sdkconfig").write_text(self.before)
        with self.assertRaisesRegex(RuntimeError, "sdkconfig"):
            self.audit()

    def test_rejects_unrelated_memory_change(self):
        (self.output / "sdkconfig").write_text(self.after.replace("4096", "1024"))
        with self.assertRaisesRegex(RuntimeError, "CONFIG_BUFFER_SIZE"):
            self.audit()

    def test_rejects_wrong_mqtt_core(self):
        (self.output / "sdkconfig").write_text(self.after.replace("CONFIG_MQTT_USE_CORE_0=y", "# CONFIG_MQTT_USE_CORE_0 is not set").replace("# CONFIG_MQTT_USE_CORE_1 is not set", "CONFIG_MQTT_USE_CORE_1=y"))
        with self.assertRaisesRegex(RuntimeError, "CONFIG_MQTT_USE_CORE"):
            self.audit()

    def test_rejects_unpinned_mqtt(self):
        (self.output / "sdkconfig").write_text(self.after.replace("CONFIG_MQTT_TASK_CORE_SELECTION_ENABLED=y", "# CONFIG_MQTT_TASK_CORE_SELECTION_ENABLED is not set"))
        with self.assertRaisesRegex(RuntimeError, "sdkconfig"):
            self.audit()

    def test_affinity_change_is_scoped_to_esp32(self):
        for target in ("esp32s3", "esp32c3"):
            self.assertEqual(toolchain.config_changes(target), {"CONFIG_MBEDTLS_HAVE_TIME_DATE": "y"})
            self.assertEqual(toolchain.target_config_addition(target), b"")

    def test_disabled_dependent_symbols_may_be_omitted(self):
        (self.output / "sdkconfig").write_text(self.after.replace("# CONFIG_MBEDTLS_TLS_SERVER is not set\n", "").replace("# CONFIG_MQTT_TRANSPORT_WEBSOCKET_SECURE is not set\n", ""))
        self.audit()

    def test_rejects_client_tls_disabled(self):
        (self.output / "sdkconfig").write_text(self.after.replace("CONFIG_MBEDTLS_TLS_CLIENT=y", "# CONFIG_MBEDTLS_TLS_CLIENT is not set"))
        with self.assertRaisesRegex(RuntimeError, "CONFIG_MBEDTLS_TLS_CLIENT"):
            self.audit()

    def test_rejects_unrequested_websocket_transport(self):
        (self.output / "sdkconfig").write_text(self.after.replace("# CONFIG_MQTT_TRANSPORT_WEBSOCKET is not set", "CONFIG_MQTT_TRANSPORT_WEBSOCKET=y"))
        with self.assertRaisesRegex(RuntimeError, "CONFIG_MQTT_TRANSPORT_WEBSOCKET"):
            self.audit()

    def test_rejects_dependency_drift(self):
        (self.output / "dependencies.lock").write_text("dependency: latest\n")
        with self.assertRaisesRegex(RuntimeError, "dependency lock"):
            self.audit()


if __name__ == "__main__":
    unittest.main()
