# Eight-player build: fork 0.3.4

This build integrates every missing upstream commit through
[v0.3.3 (`bf08ba1`)](https://github.com/Karimov-Akbar/sa-coop/releases/tag/v0.3.3),
with **eight players total: one host and seven partners**. The commit inventory,
compatibility decisions and validation are in
[the integration report](testing/upstream-0.3.3.md).

## Setup and update

Install this build's `coop.asi` and matching `coop/coop-server.exe` on every
machine. Fork 0.3.4 uses **protocol 55**; neither the earlier fork's protocol 24
nor upstream v0.3.3's protocol 54 is compatible. Animation packs are included in
`coop/packs`; optional custom IFPs go into `coop/anims` on each participating machine.

The F9 host page selects 2–8 players, default 8. The selected value is saved as
`[connect] max_players` in the client INI and passed to the server. If that key is
absent, an existing `[server] max_players` in `coop/server.ini` is respected.
Change an old two/four-player limit to eight when upgrading. The desktop launcher
writes its capacity into both INIs. A dedicated server defaults to eight and
accepts `coop-server.exe --max 8`; values above eight are capped.

Back up the host's saves and `coop/server-data` (or `coop/launcher-server-data`)
before upgrading. Existing fork profile versions 1–4 and 6 are readable; new
profiles use version 7 and also preserve bought wardrobe items. Downgrading after
saving a v7 profile requires restoring the backup. Upstream profiles with the
same version numbers use different layouts and are not interchangeable.

Profiles and cash are kept per host game. Game ID 0 retains legacy paths/account
keys; new games get their own IDs. Returning to an older game restores its own
profile and balance. Garages follow upstream's shared behavior: anyone may store
and retrieve cars. Personal safe-house ownership remains per partner profile.

## Behavior with eight players

Remote characters have independent player data; GTA's second engine slot is
borrowed for processing, never indexed by a network player ID. Vehicle and ped
IDs keep separate 12-bit namespaces for all eight owners.

Story missions start when all active players are in the same marker and interior;
clients silent for more than five seconds do not block the start indefinitely.
Side jobs and races include the players who accepted their invitations. Pool,
contests and paired animations remain activities for two within the larger session.
Normal vehicle seat counts remain unchanged: use additional mission vehicles.

The first three partners use upstream's authored cutscene paths. Extra partners
receive distinct host-selected spots checked against the shot's map and actors.
If no safe place exists, a character is hidden for that shot. This does not add
four more authored walking paths to all 130 cutscenes.

## Verification

```text
python tools/build.py dist
python tools/fsd_check.py
python tools/i18n_check.py
python tools/test_sync_primitives.py
python tools/test_network_state.py
python tools/test_server_capacity.py
python tools/test_street_racing.py
python tools/test_street_racing_network.py
node --test launcher/test/*.test.cjs
```

The capacity integration uses actual ENet clients and the built server: eight
connections, rejection of the ninth, slot reuse, smaller limits, upper cap,
45 distinct feature messages from ID 7 to all seven peers, protocol rejection,
cheat policy and separate balances when switching host games. It runs under
Wine on Linux and natively on Windows.

These automated checks do **not** run GTA's renderer, SCM missions, physics or
animations. Eight live game instances still need the gameplay checks listed in
the integration report before this is treated as a validated gameplay release.
