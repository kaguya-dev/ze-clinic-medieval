# Ze Clinic if He Borned in a Mediavel Era

A Swamp Attack–style defence shooter. You play a plague doctor guarding his
clinic against rats, crows and mutated horrors, night after night — alone or
with up to three colleagues in **online co-op (Steam) or LAN co-op**.

Written in C++17 with **SDL3 as the only dependency**. All pixel art, music and
sound effects are generated procedurally at startup, so there are no asset
files.

## Download

Grab the latest build from the [Releases](../../releases) page:

- **Windows:** `ZeClinic-windows-x64.zip` → extract → run `zeclinic.exe`
- **Linux:** `ZeClinic-linux-x86_64.tar.gz` → extract → run `./zeclinic`

Both are single self-contained executables (SDL3 is linked in statically).
Release packages also include the Steam API library (`libsteam_api.so` /
`steam_api64.dll`) next to the executable when it could be fetched at build time.

## Co-op multiplayer

Up to 4 doctors defend the same clinic. The host runs the simulation; everyone
else streams input and receives snapshots (30 Hz) plus cosmetic events, with
local prediction for your own movement and shots.

- **Steam** (uses Valve's public *Spacewar* test app, id 480, so no store page
  is needed): *Play Co-op → Host Steam Lobby* (friends only) or *Host Public
  Steam Lobby*. Invite friends from the lobby's **Invite** button or the Steam
  overlay; "Join game" in the friends list and `+connect_lobby <id>` work too.
  *Browse Games* lists public lobbies. Steam must be running and
  `libsteam_api.so` / `steam_api64.dll` must sit next to the executable (it is
  loaded at runtime — copy it from the Steamworks SDK `redistributable_bin`
  if your package doesn't have it). Without it the game still runs; Steam
  options are just greyed out.
- **LAN / direct IP**: *Host LAN Game* opens UDP port 27960. Others use
  *Browse Games* (LAN discovery) or *Join by Address* (`host` or `host:port`).

Co-op mechanics:
- **Shared barricade, separate purses.** Coins go to whoever earned them; the
  wave bonus goes to everyone. Gift coins to colleagues in the shop.
- **Downed, not dead.** At 0 HP you go down and bleed out over 30 s. A colleague
  holding **F** next to you revives you. Everyone left standing rises after the wave.
- **Repair mid-wave:** hold **F** at the stakes to repair (1 coin per 2 HP).
- **Ping / mark:** middle mouse or **V** pings a spot; pinging a foe marks it
  (+15% damage from everyone for 5 s).
- **Crossfire:** hitting a foe a colleague just hit deals +20% and earns them an assist.
- **Last stand:** the last doctor standing gets +25% damage and fire rate.
- Enemy numbers and toughness scale with the number of doctors. Late joiners
  drop in with a purse matching the run's progress.
- Ready-up in the shop (the host can force start), chat with **Enter/T**,
  scoreboard on **Tab**.

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
| Left mouse       | Shoot (hold for Repeater Bow, Censer, Storm Rod, Leech Launcher) |
| WASD             | Move around the yard behind the barricade          |
| Right mouse / G  | Throw a fire flask at the cursor                   |
| F (hold)         | Revive a downed colleague / repair the stakes      |
| Middle mouse / V | Ping (marks the foe under the cursor)              |
| E                | Place a bear trap at the cursor                    |
| Q                | Drink a healing tonic                              |
| R / Space        | Reload. Press again in the **gold zone** for a perfect reload |
| 1–9, 0 / wheel   | Switch weapon                                      |
| Enter / T        | Chat (online)                                      |
| Tab              | Scoreboard                                         |
| Esc / P          | Pause / menu (online the night goes on)            |
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
- **Plague knights** hide behind tower shields: body shots from the front
  barely scratch them. Go for the head, or use saws, fire, gas, blasts or lightning.
- **Ghouls** leap over the stakes and go straight for the doctors.
  **Cultists** heal their flock. **Plague bats** swarm and dive.

### Stages and bosses
Every 5 waves the night moves on, and each stage ends with a boss. After the
fourth stage the cycle repeats, harder, with an extra boss on each boss wave.
Stages you've reached can be picked as a starting point, with a coin stipend.

1. **Plague Town** — The Rat King (summons his brood)
2. **Blighted Marsh** — The Abomination (spawns bloaters, spits acid volleys)
3. **Burning Cathedral** — The Plague Bishop (calls down hellfire pillars on every
   doctor — move! — summons cultists, teleports, hurls holy fire)
4. **Frozen Abbey** — The Pale Horseman (charges the stakes or tramples the yard,
   is stunned and vulnerable after each charge, hurls scythes while retreating)

Each stage has its own backdrop, weather (rain, marsh wisps, embers, snow), music and ambience.

### Arsenal
Flintlock · Blunderbuss (7-pellet spread) · Repeater Bow (piercing, full-auto) ·
Brimstone Censer (flamethrower, sets enemies ablaze) · Hand Cannon (explosive shells) ·
Arquebus (pierces every foe in a line) · Storm Rod (chain lightning, stuns) ·
Bonesaw Launcher (saws bounce along the ground, slip under shields) ·
Miasma Bellows (lobbed gas clouds that poison and slow) ·
Leech Launcher (hits heal you and your most wounded colleague)

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
| `Game.cpp`        | Run flow, waves, input, the doctors, shop purchases         |
| `GameCombat.cpp`  | Authoritative combat: enemy AI, bosses, projectiles, hazards|
| `GameFx.cpp`      | Replicated cosmetic events (`fx*`) and particles            |
| `GameNet.cpp`     | Co-op sessions, lobby, snapshots, client prediction         |
| `Net.cpp`         | Transport: Steam (runtime-loaded flat API) and UDP LAN      |
| `Proto.hpp`       | Wire format and message ids                                 |
| `GameRender.cpp`  | Layered rendering, dynamic lighting, HUD                    |
| `Menu.cpp`        | Title screens, co-op browser, lobby, shop, pause, game over |
| `Autotest.cpp`    | Scripted test runs and bots                                 |
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
# Honest bot that plays until it dies and logs per-wave stats (ZECLINIC_STAGE=0..3 picks the start)
SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy ZECLINIC_AUTOTEST=play ./build/zeclinic
# Two-process LAN co-op test: host + client bots play three waves together
SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy ZC_NO_STEAM=1 ZECLINIC_AUTOTEST=host:/tmp/h ./build/zeclinic &
SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy ZC_NO_STEAM=1 ZECLINIC_AUTOTEST=join:/tmp/j ./build/zeclinic
# Steam smoke test (needs Steam running and libsteam_api next to the binary)
ZECLINIC_AUTOTEST=steam:/tmp/s ./build/zeclinic
```
`ZC_NO_STEAM=1` skips Steam entirely; `ZC_NET_DROP=0.3` simulates 30% packet loss on LAN.
Autotest runs never write to the save file.
