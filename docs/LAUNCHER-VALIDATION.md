# Launcher / mod integration audit

Local environment: Windows; launcher 0.2.2; mod 0.2.5-mp10; game protocol 18.

Fixed during the audit:

- The game host process used its nickname as the server name, overriding the configured lobby name.
  The launcher now writes `connect.server_name`, and the mod's shared command builder uses it.
- Embedded quotes and trailing backslashes in Windows process arguments could corrupt names/passwords.
  Arguments are now quoted with the Windows command-line escaping rules.
- INI readers strip outer quotes. Both launcher writes and native INI saves preserve passwords that
  contain their own enclosing quotes. UTF-8 byte limits match network string limits.
- The native nickname sanitizer omitted Ukrainian letters such as `і`, `ї`, `є`, `ґ`.
  It now uses Windows Unicode character classification.
- The launcher reported game membership as soon as a process spawned. The mod now writes status
  correlated with a unique launch token; authentication must succeed before the lobby marks it connected.
  Native rejection, disconnect and early exit are reported instead of a false successful connection.
- Server changes now wait for lobby acknowledgement; a password change reaches already-joined clients.
  Invalid settings do not partially change the room.
- Lobby connection results now include the initial roster, avoiding a stale/null room snapshot.
- Dead player skins are retried after respawn; newly joined peers receive appearance updates promptly.
- Wine launch overrides cover the supplied vorbis loader and common dinput8/dsound ASI loaders.

Validation commands:

```powershell
python tools/build.py dist
python tools/build_launcher_probe.py
python tools/test_server_capacity.py
python tools/fsd_check.py
python tools/i18n_check.py
cd launcher
npm test
npm run test:ui
npm run test:native
```

Coverage includes 8 ENet clients and capacity/refusal/relay/reused slots; UDP discovery; 8 lobby clients;
late joining; identity changes; password and host permissions; ZIP install/rollback/traversal; native
reading of launcher settings; actual server authentication from two launcher-started test processes;
per-session identity; exit and rejoin. The native test installs the actual `dist/SA-COOP.zip`.

Limits: the native test substitutes a headless test executable for GTA. It uses production config,
command-line and server code but does not execute GTA's renderer, engine addresses, player skin
application, story scripts, saves, voice devices or real gameplay. No live Linux/Wine or cross-machine
Radmin/Hamachi session was available for this audit. Those still need a real-game acceptance test.

For a real-game test, install the same ZIP on all machines; verify the selected skins on both screens,
new game and save slots, friendly fire, voice volume/microphone, joining during play, disconnect/rejoin,
and restoring skin after death/world reload. Use `coop/launcher.log`, `launcher-crash.log`,
`launcher.ini.status.json` and `launcher-server-data/server.log` to investigate failures.

Skin reset regression: choose a non-CJ skin in the launcher, start a new hosted game and finish or skip
the prologue. After CJ gets on the bike, exit the vehicle and verify the selected skin on both screens.
Join with a different skin and an existing saved profile; the launcher choice must apply after the profile.
Repeat after death, a world reload, disconnect/rejoin and a story script restoring CJ on the same ped.
Vehicle/cutscene changes must wait until on foot and outside the cutscene. An unchanged skin must not
rebuild each frame. Include CJ (0), a regular ped (105) and a special character (65); inspect
`Selected skin ... applied` in the client log. These engine scenarios require a real GTA session.

Story/appearance fixes (0.2.6-mp12, protocol 20): the launcher immediately saves a changed story-start
selection and waits for acknowledgement before starting. Explicit new-game/save-slot choices also apply
when GTA already has a world loaded. Vanilla clothes rebuilding is restricted to model 0, so CJ's clothes
builder cannot replace another skin's mesh or attach animations to the wrong skeleton.

Cutscenes use a separate clone of the host's selected skin for the story's player actor. GTA binds the scene's
animation to the cloned rig through its normal cutscene/model loading path; a live player's skeleton and
matrix array are never borrowed. The source model/TXD is pinned until cleanup. The host sends individual
collision-checked crew spots with the shot's clock; clients place their local player and other crew members
at those spots. Upgrade both server and clients:
the cutscene integer field now carries the host skin on LOAD and player ID + 1 on SPOT (0 is the shared shot).

Regression checks: `node --test launcher/test/*.test.cjs` includes immediate story selection, waiting for
acknowledgement and blocking launch on a failed save. The 22 launcher tests and the release client/server/
loader build pass. Completed Linux/Wine runs exercised skins 105 and 65 in PROLOG1/PROLOG3 and a switch
from 105 back to CJ before the following cutscene, including restoring the original cutscene template.
Screenshots and logs are in `build/validation/`. The release archive excludes the temporary probe module.
Real-game acceptance still needs all eight players, late joining and coordinated skipping; eight-player
launcher/lobby tests do not cover those engine scenarios.

Executable compatibility (launcher 0.2.3): PE32/x86, image base and the same
0x82457C / 0x94BF signature as native Core::Init are checked, replacing the
14,383,616-byte size restriction. This accepts matching 1.0 US variants of
different sizes; it does not port game hooks to Steam, 1.01 or other layouts.
The locally selected 15,806,464-byte executable has a different signature
(0x07E0E856); its version metadata is absent. It remains unsupported.
Synthetic PE fixtures validate this check; they cannot prove compatibility of
arbitrary modified executables or gameplay.

Launcher 0.2.6 / mod mp11, protocol 19: 299 stock logical skin IDs (0?299
except unused 74), offline thumbnails, searchable paginated selectors. Special
characters use available GTA slots 290?299, preserve referenced models, and
transmit logical IDs rather than local streaming-slot numbers. If story scripts
occupy all slots, applying a special appearance waits for an available slot.
SA-MP-only additional models 300?311 are excluded because stock GTA has no such
assets. Native D3D9 widget screenshots under launcher/test-results/native-theme
are offscreen previews, not screenshots of gameplay. Live testing of switching,
eight different special characters and mission/cutscene interactions is pending.
