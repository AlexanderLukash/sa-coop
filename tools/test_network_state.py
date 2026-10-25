"""Compile and run client packet regression tests without GTA (Wine on Linux)."""
import os
import subprocess
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
binary = ROOT / 'build/release/bin/network-state-test.exe'
compiler = ROOT / 'toolchain/llvm-mingw/bin/i686-w64-mingw32-clang++.exe'
subprocess.run([str(compiler), '-std=c++20', '-static', '-ffunction-sections', '-Wl,--gc-sections',
                '-I' + str(ROOT / 'src'), '-I' + str(ROOT / 'src/client'),
                str(ROOT / 'tools/network_state_test.cpp'),
                str(ROOT / 'src/client/entities/player/PlayerState.cpp'),
                str(ROOT / 'src/client/entities/vehicle/VehicleState.cpp'), '-o', str(binary)], check=True)
subprocess.run(([] if os.name == 'nt' else ['wine']) + [str(binary)], check=True, timeout=30)
print('Client packet regression tests passed.')
