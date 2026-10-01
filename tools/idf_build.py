"""Build one of the supported hardware profiles with native ESP-IDF tools."""

from __future__ import annotations

import shutil
import os
import subprocess
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parent.parent
TARGETS = (
    "esp32", "esp32s3", "esp32c3", "esp32c5", "esp32c6", "esp32c61",
    "esp32h2", "esp32h21", "esp32h4",
)
PROFILES = {
    f"{target}_{size}mb": (target, f"sdkconfig_{size}mb.defaults")
    for target in TARGETS for size in ((4,) if target.startswith("esp32h") else (4, 8))
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


def migrate_coredump_stack(sdkconfig: Path) -> None:
    """Give older generated profiles a separate stack for crash reporting."""
    if not sdkconfig.exists():
        return
    current = sdkconfig.read_text()
    updated = current.replace(
        "CONFIG_ESP_COREDUMP_STACK_SIZE=0\n",
        "CONFIG_ESP_COREDUMP_STACK_SIZE=2048\n",
    )
    if updated != current:
        sdkconfig.write_text(updated)


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
    idf_path = Path(os.environ.get("IDF_PATH", "")) / "tools" / "idf.py"
    if idf_py is None and idf_path.is_file():
        idf_command = [sys.executable, str(idf_path)]
    elif idf_py is not None:
        idf_command = [idf_py]
    else:
        print(
            "idf.py was not found. Activate an ESP-IDF environment first.",
            file=sys.stderr,
        )
        return 127

    target, profile_defaults = PROFILES[profile]
    project_dir = PROJECT_ROOT
    build_dir = PROJECT_ROOT / "build" / profile
    sdkconfig = build_dir / "sdkconfig"
    migrate_coredump_stack(sdkconfig)
    defaults = f"{project_dir / 'sdkconfig.defaults'};{project_dir / profile_defaults}"
    if target in {"esp32", "esp32s3"}:
        defaults += f";{PROJECT_ROOT / 'sdkconfig_xtensa.defaults'}"
    if target == "esp32c5":
        defaults += f";{PROJECT_ROOT / 'sdkconfig_c5_usb.defaults'}"
        migrate_c5_console(sdkconfig)
    actions = sys.argv[2:] or ["build"]

    command = [
        *idf_command,
        "-B",
        str(build_dir),
        f"-DIDF_TARGET={target}",
        f"-DSDKCONFIG={sdkconfig}",
        f"-DSDKCONFIG_DEFAULTS={defaults}",
        *actions,
    ]
    return subprocess.run(command, cwd=project_dir, check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
