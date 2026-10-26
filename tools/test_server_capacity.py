"""Build first with tools/build.py; test the real server without launching GTA."""
import os
import shutil
import socket
import subprocess
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build/release"
RUNNER = [] if os.name == "nt" else ["wine"]
HIDE_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)


def main():
    if RUNNER and shutil.which(RUNNER[0]) is None:
        raise SystemExit("Wine is required to run the Windows server integration test on this platform.")
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
        (None, None, 8, "rules-on"), (None, None, 8, "rules-off"),
    ]:
        with tempfile.TemporaryDirectory(prefix="sacoop-capacity-") as directory:
            base = Path(directory)
            config = base / "server.ini"
            config.write_text("[server]\ninternet = 0\n" +
                              (f"max_players = {configured}\n" if configured else ""))
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                sock.bind(("127.0.0.1", 0))
                port = sock.getsockname()[1]
            command = RUNNER + [str(BUILD / "bin/coop-server.exe"), "--config", str(config),
                       "--data", str(base / "data"), "--port", str(port)]
            if maximum is not None:
                command += ["--max", str(maximum)]
            if protocol.startswith("rules-"):
                command += ["--allow-cheats", "1" if protocol == "rules-on" else "0"]
            with (base / "output.log").open("w") as output:
                process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=output,
                                           stderr=subprocess.STDOUT,
                                           creationflags=HIDE_WINDOW)
                try:
                    log = base / "data/server.log"
                    deadline = time.monotonic() + 10
                    while not log.exists() or "Команды консоли" not in log.read_text(encoding="utf-8", errors="replace"):
                        if process.poll() is not None or time.monotonic() > deadline:
                            raise RuntimeError("Server failed to start: " +
                                               (log.read_text(encoding="utf-8", errors="replace") if log.exists() else "no log"))
                        time.sleep(0.05)
                    print(f"config={configured}, --max={maximum}, protocol={protocol}", flush=True)
                    try:
                        subprocess.run(RUNNER + [str(test), str(port), str(capacity), protocol], check=True, timeout=30)
                    except (subprocess.CalledProcessError, subprocess.TimeoutExpired):
                        print(log.read_text(encoding="utf-8", errors="replace"), flush=True)
                        raise
                finally:
                    process.terminate()
                    process.wait(timeout=5)
    print("All server capacity checks passed.")


if __name__ == "__main__":
    main()
