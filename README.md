# chess_viewer

Minimal SDL2 PGN chess viewer that plays random games from the `games/` folder.

Inspired by Willy Hendriks in *Move First, Think Later*:

>Apparently, looking for good moves does not have to be guided by language. Is it possible to improve your chess without language?
>
>A form of training that refrains from what can be called 'conceptual' learning, is the following: you let your chess program play instructive games at a set speed (say five to ten seconds per move), without commentary, explanations or lines. Headphones with some nice background music are allowed ... the idea - watching without the necessity of 'conscious' processing - can be fruitful.




## Requirements
- C compiler (MSVC, clang, or gcc)
- CMake 3.16+
- SDL2
- SDL2_image

## Build

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

## Run
Run from the repo root (or keep `games/` and `pieces/` next to the executable):
- Windows (vcpkg Release): `build\\Release\\chess_viewer.exe`
- Other builds: `./build/chess_viewer`

The program loads a random PGN from `games/` and PNG assets from `pieces/`.

## Windows Binary Releases
Professional standard is to keep binaries out of git and publish them via GitHub Releases.
Package a Windows zip with:
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
