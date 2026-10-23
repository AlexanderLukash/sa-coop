# Mission synchronization regression checks

Use the rebuilt client on every player. These checks require running GTA SA;
compilation and the portable sync tests do not verify native engine behavior.

- With two players, start Sweet & Kendl. Let the host reach the cemetery
  transition. Both players should move to the cemetery, including when the
  partner was on Grove Street. Repeat with a partner in a vehicle.
- Leave the partner far from the host while the host rides the mission bicycle.
  The party should receive only the required additional bicycle. Wait at least
  30 seconds: the vehicle count should stop growing. Finish/fail the mission and
  check that unused party vehicles are removed or released.
- During Ryder's scene after the haircut, both players should see the host's
  camera and lose movement control until the scene ends. Hold Space before the
  scene begins and verify it does not cancel the partner's camera.
- Check dialogue outside cinematic cutscenes, including consecutive lines that
  reuse an audio slot. Both players should hear it. Test a cold audio-bank load
  and disconnect during speech; the partner's audio should be cleared.
- Load a save/new game that creates a train before gameplay becomes ready, then
  connect the partner. Check that both see the train and its carriages moving
  along the same track. Reload with the connection active and repeat.
- Outside missions, ordinary driving and entering an interior should not pull
  other players to the host. Independent traffic should still be cleaned up.

Known scope: mission moves are detected around execution of mission scripts
(area change or a displacement greater than 15 game units). Mission dialogue
replication covers GTA's native mission-audio slots; other speech paths need
separate verification. Joining during an already playing line does not replay it.
