# Space Shooter

A retro arcade space shooter for Windows, written in C++ using the Win32 GDI.

## Building

Double-click `build.bat` (requires MinGW g++ or MSVC from Visual Studio).

Or compile manually:

```
g++ -O2 -std=c++17 -o space_shooter.exe src\main.cpp src\game.cpp src\console.cpp src\sound.cpp src\render.cpp -lgdi32 -lwinmm
```

The game looks for `sounds\shoot.mp3`, `sounds\warning.mp3`, and the fonts inside the `Fonts\` folder next to the executable.

## Controls

- **WASD / Arrow keys** - move
- **Space** - shoot
- **B** - laser beam
- **G** - grenade
- **H** - repair kit
- **J** - time freeze
- **Esc** - pause
- **F1** - developer console

## Features

- 50 levels of action (plus endless mode with **E** on the menu)
- Bosses every 5 levels, each with unique attack patterns
- Ship that evolves its shape and colors as you level up
- Power-ups and consumables, combo scoring, score gems
- High score table saved to `space_shooter_save.dat`

# Geometry Dash (`geometry_dash.cpp`)

A Geometry Dash-style rhythm platformer in the same folder.

## Building

Double-click `build_geometry_dash.bat`, or compile manually:

```
g++ -O2 -static -o geometry_dash.exe geometry_dash.cpp -lgdi32 -lwinmm -lgdiplus -lcomdlg32
```

No external assets needed - music is generated on first run.

## Controls

- **Space / Up / W / Z / Shift / click** - jump (hold to keep jumping)
- **P / Esc** - pause (practice mode toggle lives in the pause menu)
- **C** - drop a practice-mode checkpoint
- **R** - restart attempt
- **M** - mute
- **F11** - fullscreen

## Features

- 50 levels across 6 difficulty tiers, levels grow longer and meaner
- Ship, ball and UFO portal sections plus gravity-flip corridors (two-triangle map reversal)
- Practice mode with checkpoints, shard pickups, speed ramp per tier
- 16 shop skins + Color Lab (10000 shards): custom colors, photo cubes, draw-your-own 12x12 cube
- Progress saved to `geometry_dash_save.dat`
