#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
system = (root / "src/core/Esp32BaseSystem.cpp").read_text()
docs_api = (root / "docs/03_api.md").read_text()
docs_diag = (root / "docs/07_diagnostics.md").read_text()

errors = []

for needle in (
    "shouldIncrementBootCount",
    "resetReason != ESP_RST_DEEPSLEEP",
    "const bool incrementBootCount = shouldIncrementBootCount(resetReason)",
):
    if needle not in system:
        errors.append(f"src/core/Esp32BaseSystem.cpp: missing deep sleep boot count guard {needle!r}")

if "deep sleep 唤醒不会增加该计数" not in docs_api:
    errors.append("docs/03_api.md: bootCount docs must exclude deep sleep wakeups")
if "deep sleep 唤醒不增加 boot_count" not in docs_diag:
    errors.append("docs/07_diagnostics.md: diagnostics docs must exclude deep sleep wakeups")

if errors:
    print("\n".join(errors), file=sys.stderr)
    sys.exit(1)

print("System boot count checks passed")
