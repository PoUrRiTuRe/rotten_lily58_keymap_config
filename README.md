# rotten Lily58 — ZMK config

ZMK (v0.3) config for a wireless Lily58 (two nice!nano v2 + two nice!view).
GitHub Actions builds the firmware: download the `firmware` artifact of the
latest run and flash each `.uf2` (double-tap reset → copy onto the USB drive).

## Custom screens (`nice_view_rotten` shield)

| Half | Top of the screen | Bottom of the screen |
|---|---|---|
| Left (central) | battery, output, **bongo cat** instead of the WPM graph: mirrored like facing you (left half = paw on the right of the screen), reaching further from its body for the two top rows, lifting its paw for a moment between two presses; paws up when typing stops, then idle animation | Bluetooth profiles, layer name (unchanged) |
| Right (peripheral) | battery, link (unchanged) | **spinning wireframe cube** (light lines on the dark screen) |

- Shield files: `boards/shields/nice_view_rotten/`; code: `src/nice_view_rotten/`
  (based on ZMK's stock `nice_view` shield, MIT).
- Bongo cat frames: the classic QMK OLED bongo cat by ObliviousGmn (as used in
  riiadesign/lily58-kiumi), cropped to 68x32 in `tools/bongo_frames.txt`
  (`#` = pixel). `tools/sprites.py` removes the cartoon impact marks of the tap frames and
  adds the far-reaching paw copies; after editing: `python3 tools/sprites.py src/nice_view_rotten/sprites.c`.
- Preview GIF (approximate, not the real display): `python3 tools/preview.py preview.gif`.
- Cube speed and size: `CUBE_FRAME_MS`, `CUBE_STEP_Y` / `CUBE_STEP_X` and the perspective factor in
  `src/nice_view_rotten/peripheral_status.c`. The animations stop when the screen
  blanks (idle), like the rest of the display.
- Inverted colors: `CONFIG_NICE_VIEW_ROTTEN_INVERTED=y` in `config/lily58.conf`.
- Back to the stock screens: replace `nice_view_rotten` by `nice_view` in `build.yaml`.
