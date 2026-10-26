# Dynamic street racing — core MVP

This implementation covers milestone 1 and the persistence foundation for later
milestones. It is an unreleased 0.2.25 / protocol 24 change. Install the matching rebuilt
client and server on all participants. The existing session limit remains eight
players; multiple races can run simultaneously, but a player can enter only one
unsettled race at a time.

## Playing

Win **High Stakes, Low-Rider**, Cesar's first road race. Public organizers then
announce races at irregular intervals and put their starts on the map. Open
**F9 → Street racing**, select an event and inspect its length, difficulty,
eligible vehicle classes, entry fee, pool, participants and remaining time.
**Show route on map** previews its fixed road checkpoints. Drive an eligible
car to within 60 metres of the start, sit in the driver's seat and register.
Registration reserves real GTA cash immediately. **Leave race** refunds the entry
fee while registration is open.

The same page creates private races: choose an unlocked route, stake, capacity
and invited players. Invitees see the same event and decide whether to accept its
terms by registering. Private races have no organizer cut or synthetic prize.
The organizer can cancel before countdown. Every start needs at least two players.

The countdown places entrants' existing vehicles on a two-column grid and freezes
those cars until the server starts the race. It does not spawn extra racing cars
or freeze spectators. The host must observe all entrants at the grid before go.
Drive through the checkpoint markers in order. The server determines finishes,
standings and payouts; there is no client RPC to declare a finish or claim a prize.

## Story and routes

The host's loaded world is the session-wide policy, rather than each participant's
personal save. In the supported GTA 1.0 US `main.scm`, CPRACE race 0 registers
`CESAR_1` and writes global **byte offset 9200** after its first win. The native
adapter reads that flag and `STAT_CITY_UNLOCKED` (181). These were checked against
the installed mission bytecode; the lowrider dancing mission does not unlock races.

| Route | Region | Minimum city level | Difficulty | Recommended stake |
| --- | --- | --- | --- | --- |
| Market Sprint | Los Santos | 0 | 1 | $100–500 |
| East Los Santos Run | Los Santos | 0 | 2 | $150–750 |
| Vinewood Climb | Los Santos | 0 | 3 | $200–1,000 |
| Red County Run | Accessible LS countryside | 0 | 2 | $100–600 |
| San Fierro Streets | San Fierro | 1 | 3 | $250–1,500 |
| Las Venturas Express | Las Venturas | 2 | 2 | $500–2,500 |

The catalog contains fixed gates on existing CPRACE road courses, not random GPS
points. Each has a stable ID, class mask, participant limits and computed length.
SF becomes eligible at the Green Sabre city unlock; LV at the SYN7 city unlock.
The selected short routes stay within their respective accessible regions.
Another event cannot reserve the same route while it is unsettled.

## GTA cash authority

The server now owns **actual GTA cash**, not a separate racing currency. Cash,
entry fees, refunds, prizes, pending income approvals and request/transaction IDs
are committed together in `street-racing.bin` under the existing server data
directory. Native money and its HUD display mirror confirmed cash, minus pending
native spending. F9's existing balance editor uses this ledger too.

- Existing server-stored profile cash is migrated once per account. Later profile
  uploads and save/profile restoration cannot replace the authoritative balance.
- The trusted host initializes its first account from its current native cash.
  A new partner without a stored profile asks the host to approve its starting cash.
- Native spending proposes a debit. The server enforces available funds, bounded
  amounts and monotonic request sequences. A rejected debit restores confirmed cash.
- Native income generated in the trusted host's game is authorized by that host.
  Shared mission rewards carry a host transaction sequence and are credited by
  the server; receiving clients do not add the reward a second time.
- Income originating in another client's native game requires approval at
  **F9 → Player balances → Income approvals**. This includes local pickups,
  gambling, side activities and money cheats unless a future server rule verifies
  their source directly. Unapproved income is not spendable. Approval is explicit
  and cannot be replayed. This policy is deliberately broader than racing.
- Loading an older GTA save does not roll back server cash. Server cash persists
  across host-world changes; the host can reset balances through the balance editor.
- Mission reward calculation excludes server balance edits, entry fees, refunds
  and racing payouts, so a race refund is not shared again as mission income.

Cash stays between $0 and $999,999,999. Credits must leave room for outstanding
refundable stakes. Settlement preflights the entire payout before crediting anyone.
If a winner cannot receive the pool without overflow, the event refunds instead.
Keep the ledger with the server's other data; do not delete it to repair an issue.
A malformed or checksum-invalid ledger stops server startup rather than granting
fresh balances. A single-writer lock prevents two servers using the same ledger.
This release retains the project's existing nickname-based player
identity and trusted-host model; it does not introduce authenticated online accounts.

## Race rules and authority

The common engine has seven phases: Scheduled, Registration, Countdown, Racing,
Finished, Settled and Closed. Public generation chooses an available catalog route,
randomizes its stake and capacity, and schedules another check at a random interval.
Private invitations are stored by account, not reusable connection slot IDs.
Snapshots of private events are not sent to uninvited spectators.

The server controls phase deadlines and financial decisions. Its trusted world
host supplies fresh observations of local and mirrored players at four Hz. Only
the host can publish world progression and observations. The engine independently
checks ordered gates, swept position segments, minimum travel times, generous
class speed ceilings, vehicle identity/model, driver status, area, health and
sample freshness. Invalid teleports do not become the next accepted position.
This limits impossible progress; it is not a replacement for a full movement
anti-cheat or an untrusted-host security model.

| Situation | Result |
| --- | --- |
| Leave/disconnect during registration | Refund that entrant |
| Too few entrants at registration deadline | Cancel and refund everyone |
| Grid preparation fails or entrant leaves during countdown | Cancel and refund everyone |
| Leave/disconnect, change cars, die, destroy the car or invalid movement during racing | DNF; stake remains in pool |
| Entrant observation absent for 10 seconds | DNF |
| Host disappears, world reloads, story locks the region, or a story mission begins | Technical cancellation and refunds |
| Finish, then disconnect | Finish and offline prize entitlement retained |
| First finisher | Others get a configurable finishing grace period |
| No finisher before abandonment/deadline | Stakes forfeited; no prize or reputation minted |
| Server crashes | Unsettled events are cancelled and refunded once on restart |
| Server restarts after settlement | Stored prizes/statistics retained; no new payout |

The MVP uses a winner-takes-pool format with zero organizer fee. Finishes within
250 ms of the fastest time tie for first and split the pool; integer remainders
are assigned deterministically by account name. An event can settle only once.
Private events cannot create cash or reputation. Reputation is awarded only in
paid public events with at least two legitimate finishers. Basic races, wins,
losses, podiums, earnings/losses, streaks and route best times persist.

## Configuration

The server reads these optional values from `server.ini`; the launcher preserves
unrelated INI sections. The number of human participants is still bounded by
`server.max_players` and each route's limits.

```ini
[street_racing]
minimum_interval_ms = 90000
maximum_interval_ms = 240000
registration_ms = 90000
countdown_ms = 8000
maximum_race_ms = 600000
after_first_finish_ms = 60000
maximum_public_events = 2
maximum_events = 8
```

Set `maximum_public_events = 0` for invitation-only racing. Public intervals clamp
to 1–3600 seconds; registration to 0.5–300 seconds; countdown to 1–15 seconds.
The first public event is considered 10–30 seconds after a host-world unlock.
The minimum entrant count is two; a one-player server does not generate races.

## Implementation and next milestones

- `common/racing/Routes.*`: immutable catalog, progression and class eligibility.
- `common/racing/RaceEngine.*`: event scheduling, sessions, checkpoint validation,
  escrow, settlement and statistics; portable and independently testable.
- `common/economy/CashLedger.h`: cash accounts, sequenced transactions, bounded
  audit history and pending host approvals.
- `common/racing/RaceProtocol.h`: bounded server snapshots and service operations.
- `server/features/street-racing`: authority checks, durable atomic commits,
  private visibility and crash recovery.
- `client/entities/economy`: native cash reconciliation and server snapshots.
- `client/features/street-racing`: trusted host observations, grid preparation,
  map markers, route previews and race HUD. The F9 page provides registration.

Garages, owned racing vehicles, purchaseable upgrades, class performance ratings,
NPC drivers/rivals, championships, podium prize formats and city-specific
reputation/unlocks are **not implemented in this MVP**. They can use the ledger,
stable route/event IDs, eligibility rules and persistent statistics without
changing the base authority model. They need separate milestones and native GTA
validation before being presented as a complete racing ecosystem.

See [street-racing testing](testing/street-racing.md) for automated and native checks.
