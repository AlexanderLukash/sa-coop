"""Build a headless test executable using the actual mod's config/INI code."""
from pathlib import Path
import subprocess
ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build/release"
compiler = ROOT / "toolchain/llvm-mingw/bin/i686-w64-mingw32-clang++.exe"
objects = list((BUILD / "obj/common").glob("*.o")) + list((BUILD / "obj/enet").glob("*.o"))
subprocess.run([str(compiler), "-std=c++20", "-static", "-municode", "-I" + str(ROOT / "src"),
                "-I" + str(ROOT / "src/client"), "-I" + str(ROOT / "third_party/enet/include"),
                str(ROOT / "tools/launcher_native_probe.cpp"), str(ROOT / "src/client/shared/config/Config.cpp"),
                *map(str, objects), "-lws2_32", "-lwinmm", "-lwinhttp", "-lshell32",
                "-o", str(BUILD / "bin/launcher-native-probe.exe")], check=True)
print("Native launcher probe built.")
