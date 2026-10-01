"""Add direct firmware image links to a GitHub Release description."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from urllib.parse import quote

from idf_build import PROFILES


START = "<!-- bc250-image-table:start -->"
END = "<!-- bc250-image-table:end -->"
PLACEHOLDER = "<!-- bc250-image-table -->"


def image_table(assets_dir: Path, repository: str, tag: str) -> str:
    rows = [
        "| Profile | Application image | Full flash image |",
        "|---|---|---|",
    ]
    base_url = f"https://github.com/{repository}/releases/download/{quote(tag, safe='')}"
    for profile in PROFILES:
        names = (
            f"bc250_ctrl-{profile}.bin",
            f"bc250_ctrl-{profile}-full.bin",
        )
        for name in names:
            if not (assets_dir / name).is_file():
                raise FileNotFoundError(f"Missing release image: {name}")
        links = [f"[{name}]({base_url}/{name})" for name in names]
        rows.append(f"| `{profile}` | {links[0]} | {links[1]} |")
    return "\n".join(rows)


def update_notes(body: str, table: str) -> str:
    section = (
        f"{START}\n"
        "## Firmware images\n\n"
        "Choose the profile matching your chip and flash size. Use the application "
        "image for web updates on 8 MB builds; flash the full image at address "
        "`0x0` over serial/USB.\n\n"
        f"{table}\n"
        f"{END}"
    )
    if PLACEHOLDER in body:
        if body.count(PLACEHOLDER) != 1 or START in body or END in body:
            raise ValueError("Release notes contain duplicate or conflicting image table markers")
        return body.replace(PLACEHOLDER, section, 1)
    if START not in body and END not in body:
        return f"{section}\n\n{body}" if body.strip() else f"{section}\n"
    if body.count(START) != 1 or body.count(END) != 1:
        raise ValueError("Release notes contain incomplete or duplicate image table markers")
    start = body.index(START)
    end = body.index(END) + len(END)
    if end <= start:
        raise ValueError("Release image table markers are out of order")
    return body[:start] + section + body[end:]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets-dir", type=Path, required=True)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--release-json", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    body = json.loads(args.release_json.read_text())["body"] or ""
    table = image_table(args.assets_dir, args.repository, args.tag)
    args.output.write_text(update_notes(body, table))


if __name__ == "__main__":
    main()
