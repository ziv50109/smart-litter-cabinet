"""Run hardware-independent regressions; this is not an ESP32 firmware build."""
from pathlib import Path
import os
import shlex
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent

def main() -> None:
    compiler = shlex.split(os.environ.get("CXX", "g++"))
    if not compiler or not shutil.which(compiler[0]):
        raise SystemExit("C++ compiler not found. Install g++/clang++ or set CXX.")
    with tempfile.TemporaryDirectory(prefix="litter-tests-") as folder:
        for source in sorted(HERE.glob("*_test.cpp")):
            executable = Path(folder) / (source.stem + (".exe" if os.name == "nt" else ""))
            subprocess.run(compiler + ["-std=c++17", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)
    subprocess.run([os.sys.executable, str(HERE / "source_invariants.py")], check=True)

if __name__ == "__main__":
    main()
