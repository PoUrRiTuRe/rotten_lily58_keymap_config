# rotten Lily58 — ZMK config

ZMK (v0.3) config for a wireless Lily58 (two nice!nano v2 + two nice!view).
GitHub Actions builds the firmware: download the `firmware` artifact of the
latest run and flash each `.uf2` (double-tap reset → copy onto the USB drive).

## Custom screens (`nice_view_rotten` shield)

| Half | Top of the screen | Bottom of the screen |
|---|---|---|
| Left (central) | battery, output, **bongo cat** (slaps the table on every key press, idle after 350 ms) | Bluetooth profiles, layer name |
| Right (peripheral) | battery, link, **rotten flesh** | **atom** beating slowly (two beats, then a rest) |

- Shield files: `boards/shields/nice_view_rotten/`; code: `src/nice_view_rotten/`
  (based on ZMK's stock `nice_view` shield, MIT).
- Sprites (1-bit): edit `tools/sprites.py`, then
  `python3 tools/sprites.py src/nice_view_rotten/sprites.c /tmp` (needs Pillow).
- Inverted colors: `CONFIG_NICE_VIEW_ROTTEN_INVERTED=y` in `config/lily58.conf`.
- Back to the stock screens: replace `nice_view_rotten` by `nice_view` in `build.yaml`.
