# Cheat codes and player balances

Install the rebuilt client and server together on every machine (protocol 23).

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
  editing and a selected player still loading. Invalid changes must not apply.
  Partners must not see the administration button.
- Reconnect a partner after a successful change; its saved profile should keep
  the edited balance. The host's own balance is stored with its normal game save.

Portable tests cover policy serialization and balance packet validation.
`tools/test_server_capacity.py` also covers server authorization, loading
targets and cheat relaying with the rule enabled and disabled. Launcher tests
cover configuration and lobby propagation; `launcher/tools/ui-smoke.cjs` covers
the persisted checkbox. Native cheat input and the GTA menu require the checks
above in a running game.
