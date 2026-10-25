# Upstream integration in fork 0.2.22

Merged upstream main through 49fb8d8 (0.3.1). The merge base was a25c519 (0.2.5).
Earlier fork ports of some upstream fixes were reconciled with the newer implementation.
The merge records upstream ancestry so future updates can identify missing commits directly.

Compatibility decisions:

- Keep eight players, independent remote player data, launcher config/status, selected
  skins and cloned host cutscene appearance, individual scene spots and mission quorum.
- Keep personal homes and per-player garage restores, emotes, radio and cooperative 8-Track.
- Use upstream's updated in-engine scene camera and mission speech implementation.
  Remove the superseded fork MissionAudio module to prevent two handlers for the same ID.
- Bump protocol 21 -> 22 for new player, vehicle, look and animation payloads.
  Keep G_PROPERTY_OWNERS=146, G_RACE=148, G_RADIO=149. Move upstream's conflicting
  G_PROP/G_PED_SPEECH/G_MISSION_POLICY/G_VEH_SCRIPT/G_INTERIOR_POP to 173..177.
- Profile version 6 extends the fork's version 4 (u32 homes before moves and side progress).
  Read versions 1..4 as before. Upstream version 5 is rejected instead of being interpreted
  as a fork profile. Do not downgrade after saving a version 6 profile without restoring backups.
- Preserve bounds/finite-value checks while accepting new phone/task-walk/door flags and
  searchlight/tow/bike state extensions. Keep generation checks and stale-state recovery.

Automated validation:

- `python tools/fsd_check.py`
- `python tools/i18n_check.py`
- `python tools/test_sync_primitives.py` (includes old home ownership/profile migration)
- `python tools/build.py dist`
- `python tools/test_network_state.py` (actual client serializers, all 64 vehicle extension
  combinations, new player flags, every truncated packet and a non-finite position)
- `python tools/test_server_capacity.py` (eight clients, 33 feature message IDs to seven
  recipients without duplicate IDs/echo, smaller capacities, slot reuse, older protocol rejection)
- `npm test --prefix launcher`
- `python tools/build_launcher_probe.py`

Manual GTA checks remain necessary; not run in this environment:

1. Host and seven partners join, change skin, drive separate cars, reconnect/reload,
   and start a mission with the fork's quorum. Check distinct player clothes/data and scene spots.
2. Test Cesar Vialpando, Life's a Beach and Home Invasion with two players. Verify score,
   music, crates, noise, van doors, masks and cleanup after mission failure or disconnect.
   These upstream minigame adapters were designed around two players; verify their behavior
   with additional session members before relying on them in an eight-player story playthrough.
3. Invite multiple partners to a side job (Y/N), finish/fail it, check shared money,
   skills/rewards and profile restoration. Test mission-marker refresh and resync on the next mission.
4. Test in-engine scenes, cutscene cars, scene audio and host skin; recheck radio, emotes,
   8-Track, property ownership and garage restore.
5. Exercise voice enabled/disabled, alternate focus/fullscreen, photo camera, shop interiors,
   corpse removal, post-Green Sabre roadblocks/wanted level and a partner's distant turf war.
6. Upgrade existing fork profiles (especially v4 with owned homes); verify homes, money,
   weapons and clothes, then save/reconnect and verify new side progress/fighting moves.
