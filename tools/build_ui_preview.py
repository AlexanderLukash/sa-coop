from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
objects=[p for p in (root/'build/release/obj/common').glob('*.o') if p.stem.endswith(('_Util', '_Log'))]
subprocess.run([str(root/'toolchain/llvm-mingw/bin/i686-w64-mingw32-clang++.exe'),'-std=c++20','-static',
    '-I'+str(root/'src'),'-I'+str(root/'src/client'),str(root/'tools/ui_native_preview.cpp'),
    str(root/'src/client/shared/ui/renderer/Renderer.cpp'),str(root/'src/client/shared/ui/widgets/Widgets.cpp'),
    *map(str,objects),'-ld3d9','-lgdi32','-luser32','-lws2_32','-lwinmm','-lwinhttp','-lshell32',
    '-o',str(root/'build/release/bin/ui-native-preview.exe')],check=True)
