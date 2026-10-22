#!/usr/bin/env python3
"""Compile and run the small portable regression suite for network sync primitives."""
import os
import shlex
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
compiler = shlex.split(os.environ.get("CXX", "c++"))
with tempfile.TemporaryDirectory(prefix="sacoop-sync-test-") as tmp:
    binary = Path(tmp) / "sync-primitives-test"
    subprocess.run(compiler + ["-std=c++20", "-Wall", "-Wextra", f"-I{ROOT / 'src'}",
                              str(ROOT / "tools/sync_primitives_test.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("Sync primitive regression tests passed.")
