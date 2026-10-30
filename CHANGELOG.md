# Changelog

## 0.2.1
- Builds on Tessera 0.4.91 and later (plugin API 0.24), which took away the key and value form of `tessera::action`:
  the target and the mode go as a JSON object, the temperature as a number. The manifest names plugin API 0.12, which
  brought those fields.
## 0.2.0
- The dial now lives on a card that a tap on the tile opens, so the screen keeps its drags (`on_step`). The tile shows the
  target and the temperature now, with the - and + keys and the mode key.
- The number is centred on the tile.
- The keys answer `on_tap` and `tap_point()`, so they have the screen's touch filter, not an LVGL event of their own.
## 0.1.0
- First version: a dial tile for a climate entity.
