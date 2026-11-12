# Upstream v0.3.3 integration into fork 0.3.4

Source: https://github.com/Karimov-Akbar/sa-coop/releases/tag/v0.3.3
Target: `bf08ba1` (the upstream tag is recorded separately as `upstream/v0.3.3`;
the fork already had its own, different `v0.3.3` tag). Merge base: `49fb8d8`.
The integration is a real merge, preserving every source commit's ancestry.
No commits after the requested release are included.

## Commit inventory

| Commit | Upstream title |
| --- | --- |
| [`99fc50a`](https://github.com/Karimov-Akbar/sa-coop/commit/99fc50a9c1d311e19f8a378a8225eefe4656a212) | Add files via upload |
| [`9576e55`](https://github.com/Karimov-Akbar/sa-coop/commit/9576e552531909d597a75828d08ec5902f301000) | Update images in README.md cutscene section |
| [`cfc1e53`](https://github.com/Karimov-Akbar/sa-coop/commit/cfc1e53214b7882c7443640db9bd9f1902fdd2b2) | Add files via upload |
| [`8bbd4f7`](https://github.com/Karimov-Akbar/sa-coop/commit/8bbd4f750f5837912d7796b6b0f960c9e3ab0cca) | Update images in README.md |
| [`f3c0a10`](https://github.com/Karimov-Akbar/sa-coop/commit/f3c0a101b54d5b676efb5f2de2076d1742150401) | Convert image markdown to HTML table format |
| [`c359628`](https://github.com/Karimov-Akbar/sa-coop/commit/c3596288b26479a18a3140bc3a217247d94634e2) | Бильярд, соревнования, подработки вдвоём и исправления |
| [`bb0b875`](https://github.com/Karimov-Akbar/sa-coop/commit/bb0b875efe2e322dbc2fe3f67d8e9200ebf826cd) | Версия 0.3.2 |
| [`70b6357`](https://github.com/Karimov-Akbar/sa-coop/commit/70b63575af533982dc56f8052633b293a6074f98) | Мелкие правки |
| [`4f73351`](https://github.com/Karimov-Akbar/sa-coop/commit/4f73351e463667ee2cdf7080b03f09ca0c35f06b) | Трое и четверо, катсцены, анимации и исправления |
| [`bf08ba1`](https://github.com/Karimov-Akbar/sa-coop/commit/bf08ba1240aef421361487052db9b9faea59bb5e) | Версия 0.3.3 |

## Adaptations

- One host plus seven partners; default capacity 8, F9 selects 2–8. Existing
  server.ini limits remain the fallback when connect.max_players is absent.
  The desktop launcher writes the selected capacity into both configurations.
- Each remote uses independent player data in the engine's shared slot 1;
  upstream's group recycling prevents eight-player sessions exhausting ped groups.
  Player IDs never index GTA's two native player slots.
- Mission starts wait for all active peers in the same area/marker, with space
  around the marker scaled by session size. Peers silent for over five seconds
  cannot hold the start indefinitely. This replaces the fork's earlier half-party quorum.
- Cutscene ranks 0–2 retain upstream's authored paths. Further partners receive
  host-selected, collision-checked spots; a partner with no safe place is hidden
  for that shot. Every partner's explicit spot/rank travels on the wire, and all
  clients use the same result. Gunner and vehicle offsets extend to seven partners.
- Upstream radio and animation protocols replace the earlier fork versions.
  Quick emotes and the J wheel remain available alongside U and paired/custom animations.
  Stock-safe shared garages adopt upstream's new serialization, capacity feedback,
  targeted restoration and saving of cars opened in another player's game.
  Garages are shared as in upstream; personal home ownership remains per profile.
- Protocol 55 rejects both the previous fork protocol 24 and upstream protocol 54.
  Keep fork C_STREET_RACING=12; C_GAME_ID=13. Keep G_PROPERTY_OWNERS=146,
  G_RADIO=149 and fork G_PROP..G_INTERIOR_POP=173..177. New feature IDs use
  183..195 without colliding with legacy balance IDs 178/179 or voice 200.
- Fork profiles v1–4 and v6 remain readable. Profile v7 appends bought wardrobe
  items after the fork's homes, fighting moves and side-progress extension.
  Upstream profiles are not interchangeable with fork profiles with the same numbers.
- Authoritative cash accounts are scoped by host game ID. Switching games preserves
  each game's balance, resets the client's cash projection and seeds a new account
  only after the partner's profile is applied. Game ID 0 keeps legacy account keys
  and profile paths. Gifts/refunds do not become story mission income.
- Retain stale-state recovery, strict packet validation, player/entity generation
  checks, queued teleports during loading, personal homes, chosen skins and host
  cutscene skin. Skin replication uses native models plus special model names,
  preserving upstream's support for limit adjusters.
- The fork's removed ambient street-racing scheduler remains disabled; upstream's
  story/side-job race participation is included.

## Automated validation

- `python tools/build.py dist`: client, server, loader and installable archive.
- `python tools/fsd_check.py` and `python tools/i18n_check.py`.
- `python tools/test_sync_primitives.py`: includes fork profile v4/v6/v7 extension migration.
- `python tools/test_network_state.py`: player and vehicle wire formats, all 64
  vehicle extension combinations, eighth-player spawn, every truncated packet,
  non-finite coordinates.
- `python tools/test_server_capacity.py`: eight real ENet clients, ninth-player
  rejection, slot reuse, lower limits and upper cap, old protocol rejection,
  45 distinct feature IDs delivered once from player 7 to all seven other peers,
  cheat policy, forbidden legacy balance RPCs and balances isolated across games.
- `python tools/test_street_racing.py`: retained cash ledger and old escrow recovery.
- `python tools/test_street_racing_network.py`: real-server cash persistence, replay
  protection and disabled ambient racing.
- `node --test launcher/test/*.test.cjs`: 27 launcher tests.
- `python tools/build_launcher_probe.py`: native config/command-line probe builds.

## Required GTA validation

Automated tests do not execute GTA's SCM scripts, renderer, animation playback or physics.
Before publishing a gameplay release, run a host and seven actual game clients:

1. Join, reconnect, change skins/weapons and confirm independent remote appearances.
2. Start missions with 2/4/8 players, different interiors, paused/loading clients;
   vote on cutscene skips, die/respawn with shared life on/off.
3. Check cutscene paths for the first three partners and distinct safe placement
   for the remaining four, transitions between scenes, hats and custom host skin.
4. Exercise Zero's three missions, mounted guns, mission vehicles/aircraft/jetpacks,
   Stowaway, Black Project, Small Town Bank and Vertical Bird.
5. Use U preview, paired animations, installed/custom IFPs and the retained J wheel.
6. Invite seven players to side jobs/races and verify checkpoints, results and rewards.
   Pool/contests/paired animations remain two-player activities inside the larger session.
7. Exchange money/weapons in F2, place pool bets, export cars, save garage cars,
   and switch between new/old host games with existing fork profiles and balances.
8. Verify voice/radio, native driver/passenger shooting, doors, mission NPCs,
   population recovery, disconnects and world reloads on LAN and with latency.
