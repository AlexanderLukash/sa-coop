"""Download reference thumbnails only; no GTA model/texture files."""
import concurrent.futures
import json
from pathlib import Path
import urllib.request

root = Path(__file__).resolve().parents[1]
destination = root / 'launcher/assets/skins'
skins = json.loads((root / 'launcher/src/skins.json').read_text(encoding='utf-8'))

def download(skin):
    target = destination / f"{skin['id']}.png"
    if target.exists():
        return
    request = urllib.request.Request(f"https://assets.open.mp/assets/images/skins/{skin['id']}.png",
                                     headers={'User-Agent': 'SA-COOP-Launcher'})
    with urllib.request.urlopen(request, timeout=30) as response:
        data = response.read()
    if not data.startswith(b'\x89PNG\r\n\x1a\n'):
        raise ValueError(f'Invalid PNG for {skin["id"]}')
    target.write_bytes(data)

with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
    list(pool.map(download, skins))
print(f'{len(skins)} previews ready')
