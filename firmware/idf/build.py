"""Build the same sketch with ESP-IDF power management enabled; never flashes a device."""
from pathlib import Path
import argparse
import os
import subprocess
import sys

PROJECT = Path(__file__).resolve().parent
DEPENDENCIES = (
    ("arduino", "https://github.com/espressif/arduino-esp32.git", "3.3.11", "189089bb76e74978dc95abebefdad42b0d421ba9"),
    ("vl53l0x/upstream", "https://github.com/pololu/vl53l0x-arduino.git", "1.3.1", "9f3773cb48d4e4e844d689cfc529a06f96d1d264"),
)

def run(args: list[str]) -> None:
    subprocess.run(args, check=True)

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--connected", action="store_true", help="select connected standby instead of the offline default")
    args = parser.parse_args()
    idf = Path(os.environ.get("IDF_PATH", "")) / "tools" / "idf.py"
    if not idf.is_file():
        raise SystemExit("Activate the ESP-IDF 5.5.5 environment first (export.sh/export.bat).")
    version = subprocess.check_output([sys.executable, str(idf), "--version"], text=True)
    if "v5.5.5" not in version:
        raise SystemExit(f"This profile is pinned to ESP-IDF v5.5.5; found {version.strip()}")
    for relative, url, tag, expected in DEPENDENCIES:
        target = PROJECT / "components" / relative
        if not target.exists():
            run(["git", "clone", "--depth", "1", "--branch", tag, "--recurse-submodules", "--shallow-submodules", url, str(target)])
        actual = subprocess.check_output(["git", "-C", str(target), "rev-parse", "HEAD"], text=True).strip()
        if actual != expected:
            raise SystemExit(f"Dependency revision mismatch: {target}. No checkout/reset was performed.")
        if subprocess.check_output(["git", "-C", str(target), "status", "--porcelain"], text=True).strip():
            raise SystemExit(f"Dependency contains local changes: {target}. Refusing to overwrite them.")
    run([sys.executable, str(idf), "-C", str(PROJECT), "-B", str(PROJECT / "build"),
         "-D", f"LC_CONNECTED={int(args.connected)}", "build"])
    config = (PROJECT / "sdkconfig").read_text(encoding="utf-8")
    for required in ("CONFIG_PM_ENABLE=y", "CONFIG_FREERTOS_USE_TICKLESS_IDLE=y", 'CONFIG_ARDUINO_VARIANT="XIAO_ESP32S3"'):
        if required not in config.splitlines():
            raise SystemExit(f"Effective SDK configuration is missing {required}; do not deploy this output.")
    print(f"Application image: {PROJECT / 'build' / 'smart_litter_cabinet.bin'}")
    print("Build output is not proof of network reachability, UART reliability or measured power savings.")

if __name__ == "__main__":
    main()
