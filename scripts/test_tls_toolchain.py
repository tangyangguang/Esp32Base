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
        self.before = "CONFIG_BUFFER_SIZE=4096\n# CONFIG_MBEDTLS_HAVE_TIME_DATE is not set\n"
        self.after = "CONFIG_BUFFER_SIZE=4096\nCONFIG_MBEDTLS_HAVE_TIME_DATE=y\n"
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

    def test_rejects_dependency_drift(self):
        (self.output / "dependencies.lock").write_text("dependency: latest\n")
        with self.assertRaisesRegex(RuntimeError, "dependency lock"):
            self.audit()


if __name__ == "__main__":
    unittest.main()
