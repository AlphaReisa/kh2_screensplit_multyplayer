# KH2FM Split-Screen Co-op

**Local two-player split-screen co-op for KINGDOM HEARTS II FINAL MIX (PC, Steam).**
Player 2 joins with a second controller and plays a **second Sora** — a real Sora built by the game itself,
with their own camera, HP/MP gauge, command menu, magic, items and Drive Forms.

> **Status: alpha.** It is playable, but some situations still crash the game (see
> [Known issues](#known-issues)). Save often.

## Features

- **Drop-in / drop-out:** hold **LB + RB** for one second on the second controller to join or leave.
  The controller rumbles to confirm (1 s on join, 2 s on leave). Goofy turns into player 2's Sora in place,
  and back when player 2 leaves.
- **Split screen** with two full cameras; it turns on by itself when player 2 joins and when player 2's Sora
  is in the area. Player 2's right stick orbits camera 2, and their Sora moves relative to camera 2.
- **Independent controls:** each controller drives only its own Sora (Steam Input's virtual duplicate
  controllers are detected and handled).
- **Player 2's own HUD:** HP/MP/Drive gauge and command menu (Attack, Magic, Items, Drive, …) in the right
  half of the screen.
- **Player 2's own data:** HP, MP and Drive/Form gauges are kept separately from player 1's; nothing mixes on
  zone loads or save points.
- **Magic** for both Soras, each with its own spell effects, so both can cast the same spell without breaking
  each other's.
- **Drive Forms** for both Soras. One Form at a time between the two players (the game keeps only one Form
  model in memory). Player 2's Sora stands in for Goofy, so player 1 keeps access to every Form; player 2's
  Forms absorb nobody. Each Sora keeps its own Form across zone changes.
- **Items:** "use on self" and party-member targets work for player 2.

## Requirements

- KINGDOM HEARTS -HD 1.5+2.5 ReMIX- on **Steam**, KINGDOM HEARTS II FINAL MIX
  (tested with the Steam release `KINGDOM HEARTS II FINAL MIX.exe`, SHA-1 `2548f1191fe6587a34947952763ce6bb4f3b5c1a`).
- Two XInput controllers (Xbox controllers, or any controller through Steam Input).
- Works alongside OpenKH / Panacea mods and LuaBackend. **Not tested with the randomizer (GoA ROM).**

## Installation

1. Download the latest release and extract it.
2. Copy `dinput8.dll` and `kh2coop_core.dll` (and optionally `kh2coop.ini`) into the game folder, next to
   `KINGDOM HEARTS II FINAL MIX.exe`
   (usually `...\steamapps\common\KINGDOM HEARTS -HD 1.5+2.5 ReMIX-`).
3. Start the game as usual. The mod writes a log to `kh2coop.log` in the game folder.

To uninstall, delete `dinput8.dll`, `kh2coop_core*.dll`, `kh2coop.ini` and `kh2coop.log`.

## How to play

| Action | Input |
|---|---|
| Player 2 joins / leaves | Hold **LB + RB** for 1 second on the second controller |
| Player 2 camera | Right stick of the second controller |
| Toggle split screen by hand | **F8** (keyboard, game window focused) |

Player 2's Sora appears wherever the game brings the party (Donald and Goofy). In areas without a party
(towns, some story locations) player 2 waits and comes back in the next area with a party.

## Configuration

`kh2coop.ini` (optional, next to the game exe):

```ini
[keys]
split=F8        ; toggle split screen by hand
repair=F7       ; developer use

[coop]
second_sora=1   ; 1: player 2 plays a second Sora (default), 0: player 2 controls Goofy (legacy)
```

## Known issues

- Some scripted fights with special cameras cancel the split screen until the fight ends.
- Talking to some characters with the split screen on can crash the game; turning the split screen off
  (F8) before the conversation avoids it. Under investigation.
- When both Soras cast Reflect at the same time only one barrier is shown.
- If player 2's Sora is knocked out it stays down until the next area (no in-area revive yet).
- After a game over and continue, player 2 keeps its previous Drive Form.
- Summons and reaction commands for player 2 are untested.

Bug reports are welcome: please attach `kh2coop.log` from the game folder.

## Building from source

Requirements: Visual Studio 2022 Build Tools (MSVC x64).

```bat
git clone --recursive https://github.com/AlphaReisa/kh2_screensplit_multyplayer.git
cd kh2_screensplit_multyplayer
src\kh2coop\build.bat loader   :: build\dinput8.dll   (the loader; copy it to the game folder once)
src\kh2coop\build.bat          :: build\kh2coop_core.dll (the mod)
src\kh2coop\build.bat debug    :: mod with diagnostics (watchpoints, crash log, extra logging)
python src\tools\package.py    :: release zip in dist\
```

The loader hot-reloads the mod: when `kh2coop_core.dll` changes it unloads the old one (player 2 leaves
first) and loads the new one, without restarting the game. By default it loads `kh2coop_core.dll` from the
game folder; for development, put the full path of your build output in `kh2coop_core_path.txt` in the game
folder.

### Project layout

| Path | Contents |
|---|---|
| `src/kh2coop/loader.cpp` | `dinput8.dll` proxy: forwards DirectInput8 and hot-reloads the core |
| `src/kh2coop/core.cpp` | core entry: installs the modules and runs the per-frame tick |
| `src/kh2coop/common.*` | logging, address translation, signature-checked hooks, keys, config, persistent state |
| `src/kh2coop/game.*` | engine interface (named offsets, game functions, handle codec) |
| `src/kh2coop/split.cpp` | split screen and camera 2 |
| `src/kh2coop/input.cpp` | player 2 input (join/leave, XInput, the game's input layer) |
| `src/kh2coop/party.cpp` | the second Sora: spawn, live Goofy swap, save data, Drive Forms |
| `src/kh2coop/cmdmenu.cpp` | player 2's command menu |
| `src/kh2coop/hud.cpp` | player 2's HP/MP gauge |
| `src/kh2coop/magic.cpp` | player 2's magic effects |
| `src/kh2coop/friend.cpp` | legacy mode: player 2 controls Goofy |
| `src/kh2coop/debug.cpp` | diagnostics (debug builds only) |
| `src/tools/` | live analysis tools (Python / PowerShell) |
| `research/` | reverse-engineering notes (Spanish) and Ghidra scripts |

The design rule of the project is to work **through the game's own systems** (its constructors, spawn
paths, HUD, menus and data) rather than overlays or imitations; `research/NOTES.md` documents every address
and finding.

## License

Copyright (C) 2026 AlphaReisa.

This program is free software: you can redistribute it and/or modify it under the terms of the
**GNU General Public License v3.0** (see [LICENSE](LICENSE)). Contributions and forks are welcome; derived
versions must stay open source under the same license.

Third-party components (MinHook, BSD 2-Clause) are listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

KINGDOM HEARTS is a trademark of Disney Enterprises, Inc. and Square Enix Co., Ltd. This is an unofficial
fan project, not affiliated with or endorsed by Disney or Square Enix. It contains no game code or assets;
you need a legally owned copy of the game.
