"""Portable deterministic race/economy regression tests; no GTA or network required."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
compiler = shlex.split(os.environ.get('CXX', 'c++'))
with tempfile.TemporaryDirectory(prefix='sacoop-racing-test-') as directory:
    output = Path(directory) / ('street-racing-test.exe' if os.name == 'nt' else 'street-racing-test')
    subprocess.run(compiler + ['-std=c++20', '-Wall', '-Wextra', '-I' + str(ROOT / 'src'),
        str(ROOT / 'tools/street_racing_test.cpp'), str(ROOT / 'src/common/racing/Routes.cpp'),
        str(ROOT / 'src/common/racing/RaceEngine.cpp'), '-o', str(output)], check=True)
    subprocess.run([str(output)], check=True, timeout=30)
