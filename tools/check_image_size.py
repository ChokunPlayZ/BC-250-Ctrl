"""Fail CI if a firmware image exceeds the application partition."""

import argparse
import csv
from pathlib import Path


def parse_size(value: str) -> int:
    value = value.strip()
    multiplier = 1
    if value[-1:].upper() == "K":
        multiplier, value = 1024, value[:-1]
    elif value[-1:].upper() == "M":
        multiplier, value = 1024 * 1024, value[:-1]
    return int(value, 0) * multiplier


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("partitions", type=Path)
    parser.add_argument("image", type=Path)
    args = parser.parse_args()

    with args.partitions.open(newline="", encoding="utf-8") as stream:
        rows = csv.reader(line for line in stream if line.strip() and not line.lstrip().startswith("#"))
        app_sizes = [parse_size(row[4]) for row in rows if len(row) >= 5 and row[1].strip() == "app"]
    if not app_sizes:
        raise SystemExit("No app partitions found")
    image_size = args.image.stat().st_size
    maximum = min(app_sizes)
    print(f"{args.image}: {image_size:,} / {maximum:,} bytes ({image_size / maximum:.1%})")
    if image_size > maximum:
        raise SystemExit("Firmware exceeds an application partition")


if __name__ == "__main__":
    main()
