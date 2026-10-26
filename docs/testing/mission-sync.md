# Mission synchronization regression checks

Use the rebuilt client on every player. These checks require running GTA SA;
compilation and the portable sync tests do not verify native engine behavior.

- Start Drive-Thru with two players. On both clients, Big Smoke, Sweet and Ryder
  must appear in the same seats, and the partner must not be able to occupy an
  NPC's seat. Check NPCs protected by SET_CHAR_CANT_BE_DRAGGED_OUT both when first
  announced and when entering an already synchronized car. Repeat with delayed
  vehicle/model loading: occupied seats must stay reserved until mirrors spawn.
- With the host and three NPCs in a four-seat mission car, two players need one
  shared support car. With two NPCs, two players need none and three players need
  one. With eight players and three NPCs, expect two support cars. Wait 30 seconds
  and check the count stays stable without extra cars created by each partner.
  Have a partner drive the mission car, then release NPC seats and check unused
  support cars are removed while occupied support cars stay available.

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

## Recovery and ownership checks (0.2.20)

- Take over an originally remote car as its driver. Have a third player reload
  their world, causing existing owners to reannounce their vehicles. Ownership
  must remain with the current driver, with no snapping back to the old owner.
- Delete only a remote vehicle's local copy using a debugger/test harness. Its
  record should remain and the copy should return at the latest received pose,
  with its colours/upgrades. Repeat before the first motion state and after a
  car first owned locally has been driven by a remote player.
- Exhaust the vehicle pool, then free a slot. Failed mirror recreation should
  retry without a null-pointer crash. A reused slot for an unrelated vehicle of
  the same model must not be moved or deleted by the old network record.
- Restart a peer's world while driving or after death. Other players should
  discard its old seat/death/control tasks and accept its new state.
- Interrupt train state delivery for 2–5 seconds. The mirror should settle and
  survive the interruption. Resume delivery: the existing copy should follow
  the current state again. A state arriving after TRAIN_GONE must not recreate
  the train within the removal-retention window (10 seconds).

- Recreate a local NPC mirror while it is aiming, animating or dead. Its current
  network state should apply to the replacement; a dead NPC must not return as
  a living invulnerable copy, and local kill loot must not drop twice.
