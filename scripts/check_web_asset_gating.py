#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "src" / "web" / "internal" / "WebAssets.cpp"


def check_markers_guarded(text: str, markers: tuple[str, ...], guard: str, errors: list[str], label: str) -> None:
    for marker in markers:
        marker_pos = text.find(marker)
        if marker_pos < 0:
            errors.append(f"WebAssets.cpp: {label} CSS marker {marker} was not found")
            continue
        while marker_pos >= 0:
            guard_pos = text.rfind(guard, 0, marker_pos)
            endif_pos = text.rfind("#endif", 0, marker_pos)
            if guard_pos < 0 or guard_pos < endif_pos:
                errors.append(f"WebAssets.cpp: {label} CSS marker {marker} must be guarded by {guard}")
                break
            marker_pos = text.find(marker, marker_pos + len(marker))


def main() -> int:
    text = ASSETS.read_text()
    errors = []
    check_markers_guarded(
        text,
        (".appevfilters", ".appestore", ".evtable", ".evdetailgrid"),
        "#if ESP32BASE_ENABLE_APP_EVENTS",
        errors,
        "App Events",
    )
    check_markers_guarded(
        text,
        (".fsummary", ".fsactions", ".fsaction", ".fsdelete"),
        "#if ESP32BASE_ENABLE_FS",
        errors,
        "FS",
    )
    check_markers_guarded(
        text,
        (".uploadpanel progress",),
        "#if ESP32BASE_ENABLE_FS || ESP32BASE_ENABLE_OTA",
        errors,
        "upload",
    )
    check_markers_guarded(
        text,
        (".logmeta", ".logframe", ".segsize"),
        "#if ESP32BASE_ENABLE_FILELOG",
        errors,
        "FileLog",
    )

    if errors:
        for error in errors:
            print(error)
        return 1
    print("web asset gating checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
