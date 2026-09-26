# wren-sdl3

A minimal C99 host that embeds [Wren](https://wren.io) and exposes a few
[SDL3](https://libsdl.org) drawing calls to it.

```text
src/main.c          C host: SDL window, Wren VM, the foreign "sdl" module
scripts/main.wren   Wren game script (copied next to the executable on build)
vendor/SDL          git submodule, pinned to release-3.4.16
vendor/wren         git submodule, wren-lang/wren main
```

## Getting the code

```sh
git clone --recursive <this repo>
# or, in an existing clone:
git submodule update --init
```

## Building

I dev on Fedora, so have only tested this there yet.

On Fedora, you need to install SDL's build dependencies first:

```sh
sudo dnf install gcc git-core make cmake \
    alsa-lib-devel fribidi-devel pulseaudio-libs-devel pipewire-devel \
    pipewire-jack-audio-connection-kit-devel \
    libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXfixes-devel \
    libXi-devel libXScrnSaver-devel libXtst-devel \
    wayland-devel wayland-protocols-devel libxkbcommon-devel libdecor-devel \
    mesa-libGL-devel mesa-libEGL-devel mesa-libgbm-devel libdrm-devel \
    dbus-devel ibus-devel systemd-devel libusb1-devel libthai-devel \
    liburing-devel zlib-ng-compat-static
```

This is SDL's full list from `vendor/SDL/docs/README-linux.md`, updated for
current Fedora. That file's `mesa-libGLES-devel` and `vulkan-devel` no longer
exist and aren't needed, since SDL bundles its own GLES and Vulkan headers.

`vendor/SDL/docs/` has READMEs for all other platforms though, so that should
with getting going on other platforms.

Then build and run:

```sh
cmake -S . -B build
cmake --build build -j
./build/game
```

## How it works

1. `main.c` opens a window and runs `scripts/main.wren` in a Wren VM.
2. The script does `import "sdl" for App, Draw, Input`. Wren asks C for that
   module through `loadModuleFn`, and C returns a tiny source string that
   declares `foreign static` methods. Any other import, like
   `import "entities/player" for Player`, loads `scripts/entities/player.wren`.
3. Wren binds each foreign method to a C function through `bindForeignMethodFn`,
   which looks it up in the `sdlBindings` table.
4. Each frame, C polls SDL events, then uses `wrenCall` to run
   `Game.update(dt)` (game logic, where `dt` is the seconds since the last
   frame, capped at 0.1) and `Game.draw()` (rendering only), then presents the
   frame. Those methods call foreign methods like `Input.keyDown`, `Draw.text`
   and `App.quit`, which run SDL code in C. Passing a wrong argument type to
   one of these is a normal Wren runtime error.

Text uses `SDL_RenderDebugText`, SDL3's built-in 8x8 font, so it needs no
font files or SDL_ttf.

To add more SDL features, declare a new `foreign static` method in
`sdlModuleSource`, write its C function, and add a line to `sdlBindings`.
