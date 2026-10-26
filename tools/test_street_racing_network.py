"""Exercise the actual ENet server, then terminate/restart it with outstanding stakes."""
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build/release'
RUNNER = [] if os.name == 'nt' else ['wine']
HIDE = getattr(subprocess, 'CREATE_NO_WINDOW', 0)
compiler = ROOT / 'toolchain/llvm-mingw/bin/i686-w64-mingw32-clang++.exe'
binary = BUILD / 'bin/street-racing-network-test.exe'
subprocess.run([str(compiler), '-std=c++20', '-static', '-I'+str(ROOT/'src'),
    '-I'+str(ROOT/'third_party/enet/include'), str(ROOT/'tools/street_racing_network_test.cpp'),
    str(ROOT/'src/common/racing/Routes.cpp'), *map(str, sorted((BUILD/'obj/enet').glob('*.o'))),
    '-lws2_32', '-lwinmm', '-o', str(binary)], check=True)
if RUNNER and not shutil.which(RUNNER[0]):
    raise SystemExit('Wine is required to run native server integration tests.')
with tempfile.TemporaryDirectory(prefix='sacoop-street-network-') as directory:
    base = Path(directory)
    config = base/'server.ini'
    config.write_text('[server]\ninternet=0\nhost=Racer0\n[street_racing]\n'
        'registration_ms=5000\ncountdown_ms=3000\nmaximum_public_events=0\nafter_first_finish_ms=10000\n')
    for mode in ('race', 'recover'):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.bind(('127.0.0.1', 0));port = sock.getsockname()[1]
        with (base/f'{mode}.log').open('w', encoding='utf-8') as output:
            server = subprocess.Popen(RUNNER+[str(BUILD/'bin/coop-server.exe'), '--config', str(config),
                '--data', str(base/'data'), '--port', str(port)], stdin=subprocess.PIPE, stdout=output,
                stderr=subprocess.STDOUT, creationflags=HIDE)
            try:
                log = base/'data/server.log'
                end = time.monotonic()+15
                # A fresh process appends to the log; allow its socket to bind before connecting.
                while not log.exists():
                    if server.poll() is not None or time.monotonic()>end:
                        raise RuntimeError('Server startup failed')
                    time.sleep(.05)
                time.sleep(.3)
                if mode == 'race':
                    duplicate = subprocess.run(RUNNER+[str(BUILD/'bin/coop-server.exe'), '--config', str(config),
                        '--data', str(base/'data'), '--port', '0'], input='', capture_output=True,
                        text=True, encoding='utf-8', errors='replace', creationflags=HIDE, timeout=15)
                    if duplicate.returncode == 0:
                        raise RuntimeError('Two servers acquired the same authoritative economy ledger')
                subprocess.run(RUNNER+[str(binary),str(port),mode], check=True, timeout=65)
            except Exception:
                print(log.read_text(encoding='utf-8-sig', errors='replace'), flush=True)
                raise
            finally:
                server.terminate();server.wait(timeout=10)
    # A corrupt authoritative money file must fail closed, not grant fresh cash.
    ledger = base/'data/street-racing.bin'
    data = bytearray(ledger.read_bytes());data[len(data)//2] ^= 1;ledger.write_bytes(data)
    result = subprocess.run(RUNNER+[str(BUILD/'bin/coop-server.exe'), '--config', str(config),
        '--data', str(base/'data'), '--port', str(port)], input='', capture_output=True,
        text=True, encoding='utf-8', errors='replace', creationflags=HIDE, timeout=15)
    if result.returncode == 0:
        raise RuntimeError('Server accepted a corrupted authoritative economy file')
    assert ledger.read_bytes() == data
print('Real server racing/economy and crash-recovery integration passed.')
