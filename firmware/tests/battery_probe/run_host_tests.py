#!/usr/bin/env python3
"""Compile/run pure probe tests plus characterization of the unmodified main engine."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=None, help="C++ compiler executable (g++ or clang++)")
    args = parser.parse_args()
    compiler = shutil.which(args.cxx) if args.cxx else (shutil.which("g++") or shutil.which("clang++"))
    if not compiler:
        parser.error("No g++/clang++ found. Install a C++ compiler or pass --cxx. This is not an ESP32 build.")
    source = Path(__file__).resolve().parent / "host" / "test_probe.cpp"
    with tempfile.TemporaryDirectory(prefix="litter-probe-") as folder:
        binary = Path(folder) / "test_probe.exe"
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
                        str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
