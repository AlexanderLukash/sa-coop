# Street racing / GTA cash regression checks

## Automated

Run `python tools/test_street_racing.py` for the portable engine/ledger suite.
It covers story/region filtering, fixed route validity, model eligibility,
single registration, stake conservation, cancellation, DNF, teleport rejection,
ties, independent events, statistics, income approval replay, cash limits,
bounded packet parsing and exactly-once crash refunds/payouts.

Build matching binaries, then run `python tools/test_street_racing_network.py`.
This runs the real server with three native ENet clients. It checks reserved host
identity, rejection of a partner's unlock/seed, host-approved cash, native spending
and overdrafts, private visibility, real-time countdown/checkpoint/finish decisions,
offline winner payout, profile-money isolation, process termination with outstanding
stakes, restart refunds, retained statistics and a corrupt-ledger startup failure.

Run the existing sync, client packet, server capacity/rules and launcher/native
integration suites as regressions. Headless clients do not execute GTA's engine.

## In GTA (required before an initial release)

Use protocol 24 clients and server on every machine. Back up the server data before
testing the new cash authority. Keep separate server data for fixtures and real play.

1. Start before Cesar's road race. F9 must describe the unlock; no event should
   appear. Complete the lowrider dancing mission only: still locked. Win High
   Stakes, Low-Rider: LS/Red County events may appear after 10–30 seconds.
2. Reload a pre-win save: events should cancel with refunds and remain locked.
   Test the Green Sabre and SYN7 city unlocks, and a partner whose own save is more
   advanced. SF/LV events must follow the host's loaded world.
3. Discover one public event with two players. Both must see the same ID, fee,
   capacity and route. Open F9, preview the route, drive to the start and register.
   Check exact GTA money/HUD debits. Leave and register again; only current entry
   fees should remain reserved.
4. Try a bike, plane, tank and an excluded class, then change vehicles after joining.
   Joining must fail or preparation cancel with a refund. In racing, a swap is DNF.
5. Let registration close with two entrants. Verify grid positions and road ground
   height for every route and car size. Both cars should unfreeze on go. Spectators
   retain controls. Exit the car during countdown; verify cancellation/unfreezing.
6. Drive all checkpoints, including a fast crossing between network updates.
   Skip a gate, teleport, reverse over an already completed gate and replay old
   positions. None should create a false finish. Destroy a car; verify DNF.
7. Finish with one entrant, then disconnect that player while the other finishes.
   Both must see consistent results, and reconnecting should restore the winner's
   actual GTA cash. Test ties and an event in which nobody finishes.
8. Run two private races concurrently on different routes with different players.
   An uninvited spectator must not see the private event. Reuse a departed player's
   connection slot for a different nickname; the new player must not inherit an invite.
9. Disconnect the host, start a story mission, reload a world, restart the server,
   and fail grid preparation. Verify refunds, car unfreezing, marker removal and
   no rewards applied twice. Repeat after a completed settlement.
10. Exercise the broader economy: native shops, food, weapons, mission rewards,
    local pickups, gambling, side jobs, save/load and the F9 balance editor.
    Spending uses confirmed cash. Remote native income waits for host approval;
    approve/reject it in F9. Profile/save money must not overwrite server cash.
    Edit cash or refund a race during a mission: it must not become an extra shared
    mission reward. Check that temporary native changes are not lost when a server
    snapshot arrives between frames.
11. Stop or deny writes to a disposable server ledger. Transactions must not be
    announced as committed. Restore storage and reconnect. Corrupt a copy of the
    ledger and start the server: it must refuse to replace the balances with zeros.

The MVP is not claimed as gameplay-verified until these checks pass. The automated
suite cannot validate collision streaming, native freeze flags, route driveability,
all game-specific money paths or interaction with third-party modifications.
