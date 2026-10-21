# Eight-player build

This working tree includes all changes from upstream
[PR #24](https://github.com/Karimov-Akbar/sa-coop/pull/24), commit
`c0d49a5062ba98754680760e8ac7d77ab103ed27` (0.2.5-mp7), plus eight-player support.

The session supports eight players total: one host and seven partners. Remote
characters use the PR's independent player data and context switching through
GTA's second player slot; network player IDs are not engine player-slot indices.
Vehicle and pedestrian IDs retain separate 12-bit namespaces for all eight owners.

New servers default to eight slots. If `coop/server.ini` already exists with
`max_players = 2` or `4`, change it to `max_players = 8`. A dedicated server can
also use `coop-server.exe --max 8`; values above eight are capped at eight.
The F9 host launcher respects `server.ini`.

The launcher skin update extends this build to `0.2.5-mp9`, protocol 18:
`G_PLAYER_LOOK` now includes a model ID before the clothes payload. The launcher
writes `[player] skin` to its separate config. Install the updated client and
server together on every machine; mp8/protocol 17 clients cannot use this update.

Install this build's `coop.asi` on every player's machine and use its matching
`coop/coop-server.exe`. The earlier 0.2.5-mp8 build used protocol 17; older clients are
rejected because their servers/client assumptions predate eight-player sessions.

Build and check on Windows:

```powershell
python tools/build.py dist
python tools/fsd_check.py
python tools/i18n_check.py
python tools/test_server_capacity.py
```

The capacity test checks actual ENet connections, the welcome roster, relay from
player ID 7 to all seven peers, refusal of the ninth player, slot reuse, configured
smaller sessions, the upper cap, and rejection of the previous protocol's clients. It does not
run GTA. Before treating this as a tested gameplay release, verify eight game
instances together: separate weapons/appearance, mission readiness, cutscene skip
voting, vehicle/ped ownership, interiors, disconnects, and reconnects. Normal
vehicle seat counts remain unchanged; use multiple vehicles when necessary.
