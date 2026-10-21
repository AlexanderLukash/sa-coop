"""Build first with tools/build.py; test the real server without launching GTA."""
from pathlib import Path
import socket
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build/release"


def main():
    compiler = ROOT / "toolchain/llvm-mingw/bin/i686-w64-mingw32-clang++.exe"
    test = BUILD / "bin/server-capacity-test.exe"
    objects = sorted((BUILD / "obj/enet").glob("*.o"))
    if not objects:
        raise SystemExit("Build the server first: python tools/build.py server")
    subprocess.run([str(compiler), "-std=c++20", "-static", "-I" + str(ROOT / "src"),
                    "-I" + str(ROOT / "third_party/enet/include"),
                    str(ROOT / "tools/server_capacity_test.cpp"),
                    *map(str, objects), "-lws2_32", "-lwinmm", "-o", str(test)], check=True)
    # Also cover existing smaller configurations and CLI clamping.
    for configured, maximum, capacity, protocol in [
        (None, None, 8, "current"), (4, None, 4, "current"),
        (2, 8, 8, "current"), (None, 99, 8, "current"),
        (None, None, 8, "old"),
    ]:
        with tempfile.TemporaryDirectory(prefix="sacoop-capacity-") as directory:
            base = Path(directory)
            config = base / "server.ini"
            config.write_text("[server]\ninternet = 0\n" +
                              (f"max_players = {configured}\n" if configured else ""))
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                sock.bind(("127.0.0.1", 0))
                port = sock.getsockname()[1]
            command = [str(BUILD / "bin/coop-server.exe"), "--config", str(config),
                       "--data", str(base / "data"), "--port", str(port)]
            if maximum is not None:
                command += ["--max", str(maximum)]
            with (base / "output.log").open("w") as output:
                process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=output,
                                           stderr=subprocess.STDOUT,
                                           creationflags=subprocess.CREATE_NO_WINDOW)
                try:
                    log = base / "data/server.log"
                    deadline = time.monotonic() + 10
                    while not log.exists() or "Команды консоли" not in log.read_text(encoding="utf-8", errors="replace"):
                        if process.poll() is not None or time.monotonic() > deadline:
                            raise RuntimeError("Server failed to start: " +
                                               (log.read_text(encoding="utf-8", errors="replace") if log.exists() else "no log"))
                        time.sleep(0.05)
                    print(f"config={configured}, --max={maximum}, protocol={protocol}", flush=True)
                    subprocess.run([str(test), str(port), str(capacity), protocol], check=True, timeout=30)
                finally:
                    process.terminate()
                    process.wait(timeout=5)
    print("All server capacity checks passed.")


if __name__ == "__main__":
    main()
