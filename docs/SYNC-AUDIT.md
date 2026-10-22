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

## Synchronization model retained

The host still owns the story and mission scripts; each game owns its local physics and NPC simulation, and the other
game mirrors those entities. Direct player damage is decided by the shooter's game and applied by the victim/owner;
mission NPC damage is applied by the NPC owner. The changes above add stale-state recovery and input validation without
changing packet layouts, ownership, or the story authority model.

## Validation and limits

The source builds into `coop.dll`, `coop-server.exe` and `SA-COOP.zip`. The portable sync primitive tests, server
capacity integration (including protocol rejection), FSD checks and localization checks pass in the current audit
branch. These checks do not run GTA's renderer, physics, SCM missions, or two live game clients. Before a public release,
run a two-player story playthrough covering mission start/finish, failed/retried missions, cutscene skip voting,
interiors, death/respawn, vehicle changes and disconnect/reconnect on both LAN and a higher-latency connection.
