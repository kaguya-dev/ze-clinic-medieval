# Ze Clinic if He Borned in a Mediavel Era

A Swamp Attack–style defence shooter. You play a plague doctor guarding his
clinic against rats, crows and mutated horrors, night after night.

Written in C++17 with **SDL3 as the only dependency**. All pixel art, music and
sound effects are generated procedurally at startup, so there are no asset
files.

## Download

Grab the latest build from the [Releases](../../releases) page:

- **Windows:** `ZeClinic-windows-x64.zip` → extract → run `zeclinic.exe`
- **Linux:** `ZeClinic-linux-x86_64.tar.gz` → extract → run `./zeclinic`

Both are single self-contained executables (SDL3 is linked in statically).

## Build from source

```sh
# Using a system-installed SDL3 (e.g. `pacman -S sdl3` on Arch)
cmake -B build -S .
cmake --build build -j
./build/zeclinic

# Self-contained build: downloads SDL3 and links it statically
cmake -B build -S . -DZECLINIC_VENDOR_SDL=ON
cmake --build build -j --config MinSizeRel
```

Pushing a `v*` tag makes GitHub Actions build Linux and Windows packages and
attach them to a release (`.github/workflows/release.yml`).

## Controls

| Input            | Action                                             |
|------------------|----------------------------------------------------|
| Left mouse       | Shoot (hold for the Repeater Bow / Censer)         |
| Right mouse / F  | Throw a fire flask at the cursor                   |
| E                | Place a bear trap at the cursor                    |
| Q                | Drink a healing tonic                              |
| R / Space        | Reload. Press again in the **gold zone** for a perfect reload |
| 1–5 / wheel      | Switch weapon                                      |
| Esc / P          | Pause                                              |
| M                | Mute                                               |
| F11 / Alt+Enter  | Fullscreen                                         |

## Mechanics

- **Headshots:** hitting the top third of an enemy deals 2× damage.
- **Active reload:** a perfect reload instantly refills the clip and *blesses* it
  (+40% damage, golden rounds). Missing it jams the gun and slows the reload.
- **Combo:** quick kills build a coin multiplier (up to +100%). Taking damage resets it.
- **Barricade:** enemies chew through the stakes first, then come for you.
  Repair and fortify it in the shop (wood → reinforced → iron).
- **Shoot the acid:** spitter globs can be shot out of the air for a coin.
- **Chain reactions:** bloaters explode on death and hurt other enemies.
  Plague rats split into ratlings.
- **Bosses** every 5 waves: The Rat King (summons his brood) and
  The Abomination (spawns bloaters, spits acid volleys).

### Arsenal
Flintlock · Blunderbuss (7-pellet spread) · Repeater Bow (piercing, full-auto) ·
Brimstone Censer (flamethrower, sets enemies ablaze) · Hand Cannon (explosive shells)

### Skins (unlocked through progress, each with a small perk)
The Physician · Crimson Cardinal · Verdant Alchemist · Ashen Inquisitor ·
Bone Venetian · Midnight Wraith · Gilded Plague

Progress is saved to SDL's pref path
(`~/.local/share/PlagueWorks/ZeClinic/save.txt` on Linux,
`%APPDATA%\PlagueWorks\ZeClinic\save.txt` on Windows).

## Code layout

| File              | Purpose                                                     |
|-------------------|-------------------------------------------------------------|
| `main.cpp`        | SDL3 callback entry points (desktop/Android/web friendly)   |
| `Game.cpp`        | Run flow, waves, combat, enemy AI, effects                  |
| `GameRender.cpp`  | Layered rendering, dynamic lighting, HUD, shop, menus       |
| `Art.cpp`         | Pixel-art sprites as strings and procedural backdrops       |
| `Audio.cpp`       | Synthesised SFX, Karplus-Strong lute music, mixer           |
| `Gfx.cpp`         | Image ops, sprite helpers, 3×5 bitmap font                  |
| `Defs.hpp`        | Tunable data: weapons, enemies, upgrades, supplies          |

Rendering happens at 384×216 and is integer-scaled. Each frame:
1. Build a lightmap: ambient light plus additive lights for lamps, muzzle flashes, fire and explosions.
2. Draw the background and multiply it by the lightmap.
3. Draw entities on a separate premultiplied layer, also lit by the lightmap.
4. Add an unlit emissive pass for glowing eyes, windows, bullets and flames, then the vignette.

### Dev: autotest / balance bot
```sh
# Scripted session that saves screenshots (BMP) into a directory
SDL_VIDEO_DRIVER=offscreen ZECLINIC_AUTOTEST=/tmp/shots ./build/zeclinic
# Honest bot that plays until it dies and logs per-wave stats
SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy ZECLINIC_AUTOTEST=play ./build/zeclinic
```
Autotest runs never write to the save file.
