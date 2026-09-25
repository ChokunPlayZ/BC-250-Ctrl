"""Build one of the supported hardware profiles with native ESP-IDF tools."""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parent.parent
PROFILES = {
    "esp32c5_4mb": ("esp32c5", "sdkconfig_4mb.defaults"),
    "esp32c5_8mb": ("esp32c5", "sdkconfig_8mb.defaults"),
    "esp32c6_4mb": ("esp32c6", "sdkconfig_4mb.defaults"),
    "esp32c6_8mb": ("esp32c6", "sdkconfig_8mb.defaults"),
}


def migrate_c5_console(sdkconfig: Path) -> None:
    """Move older generated C5 profiles from UART0 to native USB input."""
    if not sdkconfig.exists():
        return
    current = sdkconfig.read_text()
    if "CONFIG_ESP_CONSOLE_UART_DEFAULT=y" not in current:
        return
    current = current.replace(
        "CONFIG_ESP_CONSOLE_UART_DEFAULT=y",
        "# CONFIG_ESP_CONSOLE_UART_DEFAULT is not set",
    ).replace(
        "# CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG is not set",
        "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y",
    )
    sdkconfig.write_text(current)


def usage() -> str:
    profiles = " | ".join(PROFILES)
    return (
        f"Usage: {Path(sys.argv[0]).name} <{profiles}> [idf.py options/actions]\n"
        "Example: python tools/idf_build.py esp32c5_4mb build\n"
        "Example: python tools/idf_build.py esp32c5_4mb -p PORT flash monitor"
    )


def main() -> int:
    if len(sys.argv) < 2 or sys.argv[1] in {"-h", "--help"}:
        print(usage())
        return 0 if len(sys.argv) >= 2 else 2

    profile = sys.argv[1]
    if profile not in PROFILES:
        print(f"Unknown profile: {profile}\n\n{usage()}", file=sys.stderr)
        return 2

    idf_py = shutil.which("idf.py")
    if idf_py is None:
        print(
            "idf.py was not found. Activate an ESP-IDF environment first.",
            file=sys.stderr,
        )
        return 127

    target, profile_defaults = PROFILES[profile]
    build_dir = PROJECT_ROOT / "build" / profile
    sdkconfig = build_dir / "sdkconfig"
    defaults = f"sdkconfig.defaults;{profile_defaults}"
    if target == "esp32c5":
        defaults += ";sdkconfig_c5_usb.defaults"
        migrate_c5_console(sdkconfig)
    actions = sys.argv[2:] or ["build"]

    command = [
        idf_py,
        "-B",
        str(build_dir),
        f"-DIDF_TARGET={target}",
        f"-DSDKCONFIG={sdkconfig}",
        f"-DSDKCONFIG_DEFAULTS={defaults}",
        *actions,
    ]
    return subprocess.run(command, cwd=PROJECT_ROOT, check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
