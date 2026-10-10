# Credits

Third-party work used by the C version under `c/`.

## Art

| What | By | Licence | Where in the tree |
| --- | --- | --- | --- |
| RPG Fantasy Battlers (Complete edition): the monsters in the RPG and the egg, baby dragon and dragon in Virtual Pet | [LimeZu](https://limezu.itch.io/fantasy-battlers) | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) | `c/assets/battlers/*.png` |
| Tiny Dungeon: the RPG's hero, shopkeeper, sword, shield, potions, chest and coin | [Kenney](https://kenney.nl/assets/tiny-dungeon) | [CC0](https://creativecommons.org/publicdomain/zero/1.0/) | `c/assets/kenney/tiny_dungeon.png` |
| Kenney Mini and Kenney Blocks fonts | [Kenney](https://kenney.nl/assets/kenney-fonts) | [CC0](https://creativecommons.org/publicdomain/zero/1.0/) | `c/assets/fonts/` |

`c/tools/gen_assets.py` converts these into `c/src/assets/assets.c`, which is therefore derived from
the same works under the same licences.

## Bad Apple!!

Bad Apple!! is a song by ZUN from the Touhou Project, arranged by Alstroemeria Records with vocals by
nomico. The shadow-art video is by Anira. `c/data/bad_apple.bin` holds that video's frames, reduced to
180x135 black and white and encoded by `c/tools/encode_bad_apple.py`; the sound is not included. This
is a non-commercial fan project.

## Trailer

The YouTube Short made by `c/trailer/build_trailer.py` uses audio that is not in this repository:

- "Hero" from Polyshade's Chiptunes
- sounds from the Retro Pixel Sound Effects Pack and from Cinematic Impacts, Risers, Whooshes, Drops
  ([GameDevMarket](https://www.gamedevmarket.net/))

## Code

| What | By | Licence | Where in the tree |
| --- | --- | --- | --- |
| Display Pack pin numbers, ST7789 init sequence and panel offsets, followed in `hal_pico.c` | [Pimoroni, pimoroni-pico](https://github.com/pimoroni/pimoroni-pico) | MIT | `c/src/platform/hal_pico.c` |
| `pico_sdk_import.cmake` | [Raspberry Pi, pico-sdk](https://github.com/raspberrypi/pico-sdk) | BSD-3-Clause | `c/cmake/pico_sdk_import.cmake` |
