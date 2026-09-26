# wren-sdl3

A minimal C99 host that embeds [Wren](https://wren.io) and exposes a few
[SDL3](https://libsdl.org) drawing calls to it.

```text
src/main.c          C host: SDL window, Wren VM, the foreign "sdl" module
scripts/main.wren   Wren game script (copied next to the executable on build)
assets/             images etc. (also copied next to the executable on build)
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

Images are PNGs loaded with SDL's built-in `SDL_LoadPNG` (new in SDL 3.4), so
there's no SDL_image dependency either. `Image` is a Wren foreign class wrapping
an `SDL_Texture`, freed when Wren garbage-collects it:

```wren
var slime = Image.load("assets/Abyss_Slime_D_Jump_1.png")  // relative to the executable
Draw.image(slime, x, y)         // top-left at (x, y), actual size
Draw.image(slime, x, y, 2)      // 2x, with crisp pixel-art scaling
System.print(slime.width)       // also slime.height

// Part of an image: the sw x sh rectangle at (sx, sy), e.g. one sprite frame.
Draw.imageRect(slime, sx, sy, sw, sh, x, y, scale)
```

`scripts/animation.wren` builds sprite sheets and looping animations on top
of `Draw.imageRect`:

```wren
import "animation" for Animation, SpriteSheet

// A single row of equal-width frames: 6 frames at 8 fps.
var idle = Animation.strip(Image.load("assets/Abyss_Slime_D_Idle.png"), 6, 8)

// A grid of 64x64 frames, one animation per row: row 2, 6 frames, 10 fps.
var sheet = SpriteSheet.new(Image.load("assets/Abyss_Slime_D.png"), 64, 64)
var third = Animation.new(sheet, 2, 6, 10)
sheet.draw(column, row, x, y, scale)   // or draw a single frame directly

idle.update(dt)      // in Game.update
idle.draw(x, y, 2)   // in Game.draw

third.looping = false  // play once and hold the last frame
third.restart()        // play it again; check third.finished / third.progress
```

Text uses `SDL_RenderDebugText`, SDL3's built-in 8x8 font, so it needs no
font files or SDL_ttf.

To add more SDL features, declare a new `foreign static` method in
`sdlModuleSource`, write its C function, and add a line to `sdlBindings`.

## Credits and Acknowledgments

Images from [ElvGames](https://elvgames.itch.io/terms) Ultimate Top Down Adventure Pack.
