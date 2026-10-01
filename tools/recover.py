"""Clear BC-250 settings over USB/serial without replacing the firmware."""

from __future__ import annotations

import argparse
import importlib.metadata
import os
import sys
from pathlib import Path


# Match the partition tables shipped with every supported target. The chip and
# flash size are detected from the connected device, not selected by the user.
LAYOUTS = {
    "4 MB": {
        "nvs": (0x9000, 0x10000),
        "phy_init": (0x19000, 0x1000),
        "zb_fct": (0x1A000, 0x1000),
        "factory": (0x20000, 0x390000),
        "coredump": (0x3B0000, 0x10000),
    },
    "8 MB": {
        "nvs": (0x9000, 0x18000),
        "otadata": (0x21000, 0x2000),
        "phy_init": (0x23000, 0x1000),
        "zb_fct": (0x24000, 0x1000),
        "ota_0": (0x30000, 0x360000),
        "ota_1": (0x390000, 0x360000),
        "coredump": (0x6F0000, 0x10000),
    },
}


def detect_layout(partitions: object) -> tuple[str, dict[str, tuple[int, int]]]:
    entries = list(partitions)
    by_name = {part.name: part for part in entries}
    actual = {name: (part.offset, part.size) for name, part in by_name.items()}
    if len(actual) != len(entries):
        raise ValueError("Partition table has duplicate labels; nothing erased")
    for name, expected in LAYOUTS.items():
        if actual == expected:
            if (by_name["nvs"].type, by_name["nvs"].subtype) != (0x01, 0x02) or \
               (by_name["zb_fct"].type, by_name["zb_fct"].subtype) != (0x01, 0x81):
                raise ValueError("Settings partitions have unexpected types; nothing erased")
            return name, expected
    raise ValueError("Partition table does not match a BC-250 layout; nothing erased")


def no_reset_mode() -> str:
    """Use the spelling supported by the active ESP-IDF esptool version."""
    major = int(importlib.metadata.version("esptool").split(".", 1)[0])
    return "no-reset" if major >= 5 else "no_reset"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="USB/serial port connected to the ESP32")
    parser.add_argument("--yes", action="store_true", help="skip the ERASE ALL prompt")
    args = parser.parse_args(argv)

    idf_path = Path(os.environ.get("IDF_PATH", ""))
    parttool_dir = idf_path / "components" / "partition_table"
    if not os.environ.get("IDF_PATH") or not (parttool_dir / "parttool.py").is_file():
        parser.error("activate an ESP-IDF environment before running this tool")

    try:
        reset_mode = no_reset_mode()
        sys.path.insert(0, str(parttool_dir))
        from parttool import PartitionName, ParttoolTarget
    except (ImportError, ValueError) as exc:
        parser.error(f"ESP-IDF partition tools are unavailable: {exc}")

    try:
        target = ParttoolTarget(port=args.port, esptool_args=[f"after={reset_mode}"])
        layout_name, layout = detect_layout(target.partition_table)
    except Exception as exc:
        print(f"Could not verify the connected BC-250: {exc}", file=sys.stderr)
        return 1

    print(f"Detected BC-250 {layout_name} partition layout on {args.port}.")
    for label in ("nvs", "zb_fct"):
        offset, size = layout[label]
        print(f"Will erase {label}: 0x{offset:x}–0x{offset + size:x} ({size} bytes)")
    print("This removes controller settings, credentials, and Zigbee state.")
    if not args.yes:
        try:
            confirmation = input("Type ERASE ALL to continue: ")
        except EOFError:
            confirmation = ""
        if confirmation != "ERASE ALL":
            print("Cancelled; no partitions erased. Press RESET to leave download mode.")
            return 1

    try:
        target.erase_partition(PartitionName("nvs"))
        target.erase_partition(PartitionName("zb_fct"))
    except Exception as exc:
        print(f"Recovery incomplete: {exc}", file=sys.stderr)
        print("Check both erases before rebooting. The chip remains in download mode.", file=sys.stderr)
        return 1

    print("RECOVERY COMPLETE. Press RESET to start the existing firmware with fresh settings.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
