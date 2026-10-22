# Synchronization audit

The audit follows the runtime path from the ENet receive queue through the protocol readers and entity mirrors, then
through mission/story handlers and the game hooks that apply state. The main areas are `Players`, `Peds`, `Vehicles`,
`Damage`, `Missions`, cutscene handling, world state and the common binary buffer. The detailed ownership rules and
supported story behavior remain in [ARCHITECTURE.md](ARCHITECTURE.md).

## Findings addressed

- A peer could stop sending state while its last controller input or vehicle controls remained active. Player inputs now
  neutralize after 350 ms without a state update. Mirrored vehicles extrapolate only briefly, then fade steering and
  throttle and settle; mirrored NPCs clear stale combat/movement tasks and stand after 750 ms without updates.
- The binary reader replaced non-finite floats with zero but kept the packet valid. A damaged coordinate could then be
  interpreted as a real move to the origin. Non-finite floats, non-canonical booleans, overflowing varints and invalid
  world positions/velocities now invalidate the relevant state packet before it reaches GTA memory or script commands.
- Entity creation and ownership announcements now verify the sender against the announced owner. Player/NPC/vehicle
  state also checks world area, seat, movement, wanted-level and weapon ranges before applying it.
- Player, NPC and vehicle state ordering each implemented timestamp comparison separately. They now share wrap-safe
  32-bit sender-clock ordering and bounded local-clock age calculations.
- Added portable regression coverage for clock wrap, state freshness/fade, world bounds and malformed binary values.
  The capacity integration test runner also supports Wine, so the actual ENet server/capacity scenario can be run from
  a Linux development environment.
- Mission checkpoints and script spheres now retain the host's latest complete set on the partner. If the partner's
  local script arrays are full when an update arrives, placement is retried each frame after slots become available.
  Reconciliation uses the host's logical slot, so a checkpoint reusing a slot cannot leave behind or confuse a stale
  marker. Incoming marker and cutscene placement data is range-checked before game commands run.
- Mission marker rendezvous also checks the player's interior/area, not only world coordinates. This avoids treating
  coincident coordinates in different interiors as the same mission start point.
- Mission rendezvous counts only fresh player states (at most three seconds old) in the same area and marker. The host
  starts when at least half of the connected session is present (rounded up), then sends reliable teleports for absent
  participants to the host's position and interior. A stale remote ped cannot count toward the quorum. The partner only
  reports a marker invitation while the host's state is fresh and in the same area.
- Ordinary entrance transitions remain local: `Entrances.cpp` synchronizes door-access flags only and never moves a
  player or changes their interior. The mission quorum teleport is limited to main-story triggers; explicit `/tphere`
  remains the only other remote-player teleport path.
- A quorum teleport received while SCM is rebuilding a world is queued for up to 15 seconds and applied when the local
  player ped is ready, rather than being lost with its one reliable packet.
- Marker invitations send a two-second heartbeat. The host clears an invitation after five seconds without an update,
  so a stalled game cannot leave a permanent false waiting marker while its network peer still appears connected.
- Mission support vehicles are sized from the active player count and the root vehicle's passenger seats. A fleet grows
  only after a player boards a script-held vehicle, which avoids cloning race opponents and NPC mission traffic. Empty
  support cars are removed as the party shrinks or after the mission releases its root vehicle.
- Ordinary safe-house purchases by a partner are saved in that partner's profile, reapplied to later host worlds and
  labeled in-world and through `/homes`. Door/garage access remains part of the shared GTA world. Host-owned and
  partner-owned garage slots are tracked separately in-session; restoration packets are targeted and returned car
  models are checked against the outstanding request. Slot ownership metadata is currently session-local; old save
  entries without metadata are treated as host-owned to prevent one player from claiming another's cars.

## Synchronization model retained

The host still owns the story and mission scripts; each game owns its local physics and NPC simulation, and the other
game mirrors those entities. Direct player damage is decided by the shooter's game and applied by the victim/owner;
mission NPC damage is applied by the NPC owner. The changes above add stale-state recovery and input validation without
changing packet layouts, ownership, or the story authority model.

At least half of the connected players must rendezvous at story markers; the host brings the rest to the same place
before the mission proceeds. Their peds, vehicles, mission NPC
damage, mission pickups/objects, checkpoint visuals, mission UI, and cutscene places are synchronized around that one
authoritative script. Partners can affect the host's mission through shared game entities while the save and outcome
remain consistent. A copied checkpoint is guidance, not a second mission script: a mission that checks only CJ's local
position or vehicle still advances from the host's actions. Making each player independently satisfy every SCM
condition needs per-mission adapters and cannot be inferred safely from generic checkpoint data.

## Validation and limits

The source builds into `coop.dll`, `coop-server.exe` and `SA-COOP.zip`. The portable sync primitive tests, server
capacity integration (including protocol rejection), FSD checks and localization checks pass in the current audit
branch. Launcher unit and native integration tests cover packaging and lobby flows. These checks do not run GTA's
renderer, physics, SCM missions, or two live game clients. Before a public release,
run a two-player story playthrough covering mission start/finish, failed/retried missions, cutscene skip voting,
interiors, death/respawn, vehicle changes and disconnect/reconnect on both LAN and a higher-latency connection.
