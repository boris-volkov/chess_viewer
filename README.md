# chess_viewer

Minimal SDL2 PGN chess viewer that plays through random games from the `games/` folder.

Inspired by Willy Hendriks in *Move First, Think Later*:

>Apparently, looking for good moves does not have to be guided by language. Is it possible to improve your chess without language?
>
>A form of training that refrains from what can be called 'conceptual' learning, is the following: you let your chess program play instructive games at a set speed (say five to ten seconds per move), without commentary, explanations or lines. Headphones with some nice background music are allowed ... the idea - watching without the necessity of 'conscious' processing - can be fruitful.

Who knows how fruitful... but I've personally found it surprisingly entertaining and interesting to just watch a high quality chess game flow by without anybody yapping about it. 

## What it does
- Plays back PGN games on a full‑screen SDL board.
- Loads PNG pieces from `pieces/` and games from `games/`.
- Supports pause, step, analysis mode, and guess‑the‑move mode.

## Quick start (Windows release zip)
1. Download the latest `chess_viewer-win64.zip` from GitHub Releases.
2. Unzip it anywhere.
3. Run `chess_viewer.exe`.

The zip already includes the required DLLs plus `games/` and `pieces/`, so no extra installs are needed.

## Build from source

### Requirements
- C++17 compiler (MSVC, clang, or g++)
- CMake 3.16+
- SDL2
- SDL2_image

### Windows (vcpkg)
1. Install vcpkg, then run:
```sh
vcpkg install sdl2 sdl2-image
```
2. Configure and build:
```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

### Windows (MSYS2 MinGW)
Use the MINGW64 shell.
```sh
pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_image mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=/mingw64 -DPKG_CONFIG_EXECUTABLE=/mingw64/bin/pkg-config
cmake --build build
```

### macOS (Homebrew)
```sh
brew install sdl2 sdl2_image cmake
cmake -S . -B build
cmake --build build
```

### Linux (Debian/Ubuntu)
```sh
sudo apt-get install libsdl2-dev libsdl2-image-dev cmake
cmake -S . -B build
cmake --build build
```

### Linux (Arch)
```sh
sudo pacman -S sdl2 sdl2_image cmake
cmake -S . -B build
cmake --build build
```

## Catalog

Press `C` for the catalog. Alongside the on-disk tree it offers views built from
a per-game index of every PGN under `games/`:

- **`[BY PLAYER]`** — every player in the collection, most games first, drilling
  into that player's games across all files.
- **`[BY YEAR]`** — the same, grouped by year.
- **`/`** — search all games by player name or year as you type.
- **A PGN file** — opens as a folder of its games, with a `[RANDOM GAME]` row
  at the top for when you just want to watch something from it.

Rows in these views are individual games, not files, so the preview boards show
that game's opening and final position. Selecting one opens exactly that game.
Containers — directories, PGN files, players, years — show no preview, since
there is no single position they stand for.

Players are listed by surname. The source PGNs tag the same person several
ways — `Ivanchuk,V` and `Ivanchuk, Vassily` were separate entries — so taking
the name before the comma both merges them and keeps rows short enough to read.

The index is built in a background thread on first run (about 4.5 seconds for
419,617 games) and cached to `games/.chess_viewer_index`, after which it loads
in well under a second. Editing, adding or removing a PGN triggers a rebuild
automatically. Deleting the cache file is safe — it is regenerated.

## Settings

Display preferences — Elo display, uncoloured mode, defense lines, board
orientation, and playback speed — are written to `settings.txt` next to the
executable as soon as they change, and restored on the next launch. Delete the
file to go back to defaults. Modes (analysis, guess) are deliberately not
persisted; they are per-game state.

## Run
Run from the repo root (or keep `games/` and `pieces/` next to the executable):
- Windows (vcpkg Release): `build\\Release\\chess_viewer.exe`
- Other builds: `./build/chess_viewer`

The program loads a random PGN from `games/` and PNG assets from `pieces/`.

## Tests

The test targets are excluded from the default build, so they can never break
a release build, and need no reconfiguring to use:

```sh
cmake --build build --target tests
ctest --test-dir build --output-on-failure
```

Each test `#include`s `chess_viewer.cpp` directly in order to reach that file's
`static` functions, so they exercise the code that actually ships rather than a
copy of the logic.

- `tests/menu_test.cpp` — command menu geometry: every pixel of each menu label
  maps back to that label's own row, separators and clicks outside the panel
  select nothing, and keyboard navigation wraps and reaches every row.
- `tests/settings_test.cpp` — settings persistence: round-trip, unknown keys
  ignored, absent keys keeping their current value, playback speed clamped to
  its legal range, malformed files surviving intact, and paths resolving next
  to the executable.
- `tests/index_test.cpp` — the game index: header parsing, byte offsets landing
  on real `[Event` lines, case-insensitive search, player and year grouping,
  cache round-trip and invalidation, corrupt caches, and concurrent access
  during a background build.

## Custom piece sets

You can create custom chess piece sets by converting SVG files to PNG format.

### Using Python script (recommended)
```sh
# Install Python dependencies and convert SVG files
python scripts/svg_to_png.py [size]

# Examples:
python scripts/svg_to_png.py        # Default 64x64 (good for standard displays)
python scripts/svg_to_png.py 128    # 128x128 (good for high-DPI displays)
python scripts/svg_to_png.py 256    # 256x256 (excellent for 4K displays)
```

### Using Windows batch script
```batch
# Requires ImageMagick to be installed
scripts\svg_to_png.bat [size]
```

### Instructions
1. Place your SVG chess piece files in the `pieces/` directory
2. Use the naming convention: `Chess_[piece][theme].svg`
   - Pieces: `k` (king), `q` (queen), `r` (rook), `b` (bishop), `n` (knight), `p` (pawn)
   - Themes: `lt` (light theme), `dt` (dark theme)
   - Examples: `Chess_klt.svg`, `Chess_qdt.svg`, `Chess_pdt.svg`
3. Run the conversion script
4. The PNG files will be created alongside the original SVG files (which are preserved)

The Python script will automatically install required dependencies. The batch script requires ImageMagick.

## Releases and packaging
Windows binaries are published via GitHub Releases to keep the repo clean.
The release zip contains:
- `chess_viewer.exe`
- SDL2/SDL2_image runtime DLLs and their dependencies
- `games/` and `pieces/`
- `README.md` and `LICENSE`

Automated releases are supported: push a tag like `v1.0.0` and GitHub Actions will build
`chess_viewer-win64.zip` and attach it to the release.

If you build with MSYS2, you can inspect dependencies using:
```sh
ldd build/chess_viewer.exe
```
