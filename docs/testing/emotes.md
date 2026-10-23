# Emotes integration (upstream PR #48)

Source: https://github.com/Karimov-Akbar/sa-coop/pull/48
Upstream head: 879f723a38aa714bc1e0835924a8d953c7217110 (AmXLoVE).

The emote catalog, wheel, configuration and animation replication have been
ported into 0.2.20 alongside our mission/vehicle synchronization changes.
The animation replication prerequisite from the PR branch is included.

Checks requiring GTA with at least two players:

1. Hold J, move the mouse to select an emote and release J. Both games should
   show the same animation. Releasing J in the center should do nothing.
2. Try `/e`, `/e wave`, `/dance`, `/sit` and `/e stop`. Check both interface
   languages and aliases. `/help` should list emote commands.
3. Keep a movement key held while selecting an emote. It should not cancel
   immediately; release and press movement/jump/aim again to cancel. Test a
   gamepad with minor stick drift.
4. Move the mouse while the wheel is open. The camera must remain still, and
   closing the wheel must not apply accumulated motion to the camera.
5. While selecting an emote, switch focus to another window, open the chat/F9
   menu, or start a mission scene. The wheel should close without playing its
   selection and return mouse input to the game/UI.
6. Change `[keys] emote_wheel` in coop.ini and restart. The configured key and
   wheel hint should agree; an existing config without the key should use J.
7. Start a looped emote, then reload the other player's world. The animation
   should be sent again and applied after the remote player is created.
   Repeat with a locally recreated remote-player copy.
8. Stop/switch emotes and disconnect. Check that idle, walking, mission cameras
   and ordinary NPC animations continue working. Emotes should be blocked in
   cars, water, while falling and during mission/cinematic scenes.

Build and portable sync checks can run without GTA. They do not validate native
animation names, mouse hooks or multiplayer visuals; those require the above
in-game checks.
