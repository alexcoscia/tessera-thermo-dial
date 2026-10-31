# Changelog

## 0.3.1
- A tile two or more cells wide and one high (2x1) shows the - key at the left and the + key at the right, with the
  number between them and under it the temperature now and the state's icon. The name stays above the number only when
  there is room for it.
- The number, the temperature now and the icon take a colour while the thermostat works: Home Assistant's amber when
  it heats, the screen's blue when it cools. Idle, off and unavailable keep the usual colours.

## 0.3.0
- A tile one cell wide and two or more high (1x2, 1x3) shows the - and + keys at the bottom, under the words; the mode
  key still needs two cells in width.
- Under the number every tile that has room shows the temperature now and the state's icon (flame, snowflake, power,
  and zzz when idle). The card uses the same icons.

## 0.2.1
- Builds on Tessera 0.4.91 and later (plugin API 0.24), which took away the key and value form of `tessera::action`:
  the target and the mode go as a JSON object, the temperature as a number. The manifest names plugin API 0.12, which
  brought those fields.
- README: a new step in "Set up" says the screen must be allowed to perform Home Assistant actions.

## 0.2.0
- The dial now lives on a card that a tap on the tile opens, so the screen keeps its drags (`on_step`). The tile shows the
  target and the temperature now, with the - and + keys and the mode key.
- The number is centred on the tile.
- The keys answer `on_tap` and `tap_point()`, so they have the screen's touch filter, not an LVGL event of their own.
## 0.1.0
- First version: a dial tile for a climate entity.
