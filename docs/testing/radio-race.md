# Radio and 8-Track validation (0.2.21)

Upstream PRs #49 and #47 by AmXLoVE; emotes from #48 were already ported in 0.2.20.
This fork assigns G_RACE=148 and G_RADIO=149, preserving G_MISSION_AUDIO=147.
Protocol remains 21: these are additional messages, with no changes to existing payloads.
All participants need 0.2.21 to use the new features.

Automated checks: `python tools/fsd_check.py`, `python tools/i18n_check.py`,
`python tools/test_sync_primitives.py`, `python tools/build.py dist`,
`python tools/test_server_capacity.py`. The capacity test checks both new message IDs
reach all seven other clients, preserve sender and payload, and do not echo to the sender.
It also covers smaller capacities, slot reuse and rejection of an older protocol.

Manual GTA checks (not executed in the development environment):

1. With 2 players, drive the same civilian car; change stations, turn radio off,
   and enter/leave as passenger. Verify track and approximate playback position match.
2. Repeat with several passengers and after changing the driver. Other cars must
   keep their own radio. Passenger retune keys and paused audio menu must not retune.
3. Select User Track Player with and without passenger MP3 files. Each player hears
   local files; a passenger with no tracks hears silence.
4. At Los Santos 8-Track, gather 2 and then 8 players within 4 m of the marker.
   Verify a 15-second wait (or early start when everyone joins), valid grid positions,
   synchronized countdown, 8 laps, checkpoints, standings and one $10,000 player prize.
5. Finish/crash/leave as race owner while riders continue; verify mission racers
   disappear, remaining riders can finish, and everyone exits with control restored.
6. Start alone, decline an invitation, join late, disconnect, and reload a save.
   Verify no unwanted teleport, stuck controls or leftover race markers.
7. Recheck story mission markers, mission audio, property ownership and J emotes.
