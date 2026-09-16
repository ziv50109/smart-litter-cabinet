from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
source = root / "firmware" / "tests" / "main_v2_host" / "visit_logic_test.cpp"
with tempfile.TemporaryDirectory() as td:
    exe = Path(td) / "visit_logic_test"
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-pedantic", str(source), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
