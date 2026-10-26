# Cheat codes and player balances

Install the rebuilt client and server together on every machine (protocol 24).

- In the launcher's server settings, disable **Дозволити чит-коди**, save,
  restart the launcher and create a lobby. The switch and room summary should
  retain the setting. Join with another player and start the session.
- On both host and partner, type HESOYAM, ROCKETMAN, a vehicle spawn code and a
  weather code. None should activate, change stats or be sent to the other player.
  Repeat with GInput installed. Chat and normal controls must still work.
- Start another session with codes enabled. Personal codes should work locally;
  world codes should still synchronize. After disconnecting, codes should work
  in single player regardless of the previous session's rule.
- As host, open F9 and **Player balances**. Select yourself, enter an exact
  amount and apply. Repeat for a partner, including zero and 999999999. The
  selected player's money and HUD should update, and the host should receive
  confirmation in chat. Other players' balances must stay unchanged.
- Check empty input, non-digits, excessive length, a player disconnecting during
  editing. Invalid changes must not apply. A balance assigned while the selected
  player is loading must appear when that player becomes ready.
  Partners must not see the administration button.
- Reconnect a partner after a successful change; its saved profile should keep
  the edited balance. Cash now comes from the server economy ledger, including
  the host's cash. Restoring a GTA save or uploading a profile must not overwrite it.
- As a partner, earn cash through a local native activity. The host should see
  an income approval in F9 → Player balances. Approve once, then try the same
  approval again: the credit must not be duplicated. Reject another request;
  its money must not become spendable. Native spending must debit confirmed cash.

Portable tests cover policy serialization and balance packet validation.
`tools/test_server_capacity.py` also covers server authorization, loading
targets and cheat relaying with the rule enabled and disabled. Launcher tests
cover configuration and lobby propagation; `launcher/tools/ui-smoke.cjs` covers
the persisted checkbox. Native cheat input and the GTA menu require the checks
above in a running game.
