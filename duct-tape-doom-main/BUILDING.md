# Modern builds

The root CMake build compiles the original game, software renderer, and WAD
loader as a C11 library. It also links a headless demo runner and regression
checks on macOS, Linux, and Windows. No game data is needed to build or test.

The optional SDL3 backend adds a playable desktop window, keyboard/mouse input,
fullscreen, sound effects and an accelerated 3D renderer built on SDL_gpu:
Metal on macOS, Vulkan on Linux and Windows, from one renderer and one set of
shaders. It draws a scene built at runtime from the WAD by `platform/scene3d/`. Music plays from a
SoundFont or an emulated OPL3 chip, and on macOS also through the system synthesizer. The headless backend remains available for
verification.

The repository contains no game data. Download Freedoom from
[freedoom.github.io](https://freedoom.github.io/) or use an IWAD from a copy of
DOOM you own. See [PLAY.md](PLAY.md) for controls. Multiplayer is not implemented.

## Build a playable app

```sh
cmake -S . -B build/play -DCMAKE_BUILD_TYPE=Release -DDOOM_BUILD_SDL=ON \
  -DDOOM_GAME_DATA="/absolute/path/to/freedoom1.wad"
cmake --build build/play --parallel
ctest --test-dir build/play --output-on-failure
```

CMake uses an installed SDL3 package or downloads and builds SDL3 v3.4.18.
For an offline build, set `CMAKE_PREFIX_PATH` to an existing SDL3 installation.
CMake also downloads the Nuked OPL3 emulator (LGPL-2.1+) and TinySoundFont
(MIT) at pinned commits; offline, set `FETCHCONTENT_SOURCE_DIR_NUKEDOPL3` and
`FETCHCONTENT_SOURCE_DIR_TINYSOUNDFONT` to copies of them. It also downloads the
GeneralUser GS SoundFont (32 MB, pinned and hash-checked) into
`build/play/soundfonts/` and copies it beside the game; `-DDOOM_SOUNDFONT=OFF`
skips it, and music then falls back to OPL (to the system synthesizer on macOS).
On macOS, game data is copied into `build/play/Play DOOM.app`. You can supply
an original DOOM/DOOM II IWAD instead. Without bundled data, the app presents
a file picker at launch. On Linux and Windows use `doom -iwad /path/to/game.wad`.
`platform/i_music.c` picks the music device. MUS and MIDI scores go through
`platform/audio/`: either TinySoundFont with a `.sf2` bank, or an emulated OPL3
chip with the WAD's GENMIDI instruments, built from the published chip and lump
documents (`platform/audio/opl_music_spec.md`). `doom-music-render <wad>`
writes a WAD's songs to WAV files through either synth without starting the
game (`-synth opl|sf2`, `-sf2 bank.sf2`, `-song D_E1M1`).
`-DDOOM_RENDER3D=OFF` builds the classic software renderer only. Without a
Metal or Vulkan driver at runtime the app starts with the classic renderer.

`DOOM_GAME_DATA` also enables an isolated gameplay test. It opens a level with
SDL's dummy display/audio drivers, moves, fires, saves, changes player state,
loads, and checks that the saved state is restored. This has passed with
Freedoom in Release and AddressSanitizer builds. The graphical menu, movement,
and firing were also checked in the native Mac window.

## 3D renderer

`platform/scene3d/` (C++17, one file per subsystem; `internal.h` lists what
they share) builds the 3D world from the original loaded map, and `platform/gpu3d/` draws it with SDL_gpu: BSP leaves are clipped into convex floor/ceiling polygons, linedefs produce
textured wall spans, and game objects produce animated, directional billboards.
Geometry uses current sector heights and texture translations so doors, lifts,
switches and animated materials follow the game simulation. WAD textures are
decoded at runtime into palette indices plus alpha coverage; no artwork is
replaced or manually converted. The GPU performs perspective mapping and depth
testing at the drawable resolution, with optional MSAA (off by default, up to
what the device supports).

World textures optionally carry an emissive mask in a third GPU channel.
Conservative name/hue rules select lamp strips, colored computer displays, lava
and nukage from the original PLAYPAL colors while leaving dark outlines and
housing shaded. The name lists in `platform/emissive.h` were audited against
Doom, Doom II, Final Doom, Master Levels, SIGIL I/II, Legacy of Rust and
Freedoom: ceiling lights (FLAT2, FLAT17, FLAT22, CEIL1_2/CEIL1_3 and variants,
MLITE, KCF_LIT), key-colored door strips, EXIT signs, consoles and fire/lava
walls are included. Lava-veined rock (RROCK, SLIME09–12, CRACKLE, ROCKRED) lights only
its red-to-yellow cracks, and the unlit `*OFF` variants never emit. The shader applies the mask as a minimum illumination, retaining
stronger dynamic lighting and the original palette effects. F4 can toggle it.
External grayscale PNGs override defaults, using `Emissive/walls/NAME.png` and
`Emissive/flats/NAME.png` beside graphics.cfg, or a directory chosen with
`-emissive /absolute/mask/directory`. Masks must match original dimensions;
invalid masks log a warning and retain automatic detection. An all-black mask
disables emission for that material. Assets are cached until restart. This is
self-illumination plus soft bloom and colored light spill. A separate half-resolution,
depth-tested emission pass selects only masked world pixels. Two separable blur
passes create the halo, composited with HUD coverage protection. Nonemissive
geometry occludes bloom sources; normal bright artwork and HUD pixels do not
seed bloom. The renderer places approximate point sources at mask-weighted
centers of wall texture repeats and the world-aligned floor/ceiling texture
repeat grid, following current animation, offsets, pegging and moving sector
heights. Lights are collected from both wall sides independently of which walls
are drawn; BSP floor polygons only test whether a repeat belongs to the sector.
Up to 24 nearby material sources share the existing wall/door occlusion checks
and sprite lighting with the 16 shot/projectile sources. Stable surface/repeat
identities retain lights through small distance-ranking changes. Budget changes
fade over 0.2 seconds, with outgoing sources releasing their slots before new
sources fade in; camera distance fades smoothly from 640 to 896 map units.
This approximates area lighting rather than physically simulating it.
F4 toggles self-illumination, bloom and light spill together.

Monitor screens get glass. `platform/screen_glass.h` finds them in computer
textures (COMP*, PLANET*, SPACEW*, TEKWALL*, CONS1_* and similar) from the
pixels: dark regions that nearly fill a rectangle, framed by lighter housing,
with readouts joined by a small closing; vent grilles are rejected. Each
glass pixel also records where it sits on its screen, in a small companion
texture. In the world shader each screen becomes a CRT dome as tall as a
quarter of its shorter side: its smooth normal takes tight highlights from
lamps, shots and the flashlight, the pane catches a thin sheen of the room's
brightness where it curves away from the eye, and the rim shades the
picture's edges in light steps. Like a weak lens the glass enlarges the
picture's middle by about 1.14x, with the edges still meeting the rim.
F4's "Glossy monitor screens" switch (`glossy_screens`) toggles it.

The original 35 Hz simulation stays in place. Native display frames can run
between ticks, with interpolation for camera position, yaw and moving sprites.
HUD/menu patch coverage is composited over the world, preserving black pixels
and transparent regions. The F4 panel saves graphics choices separately from
the original game configuration. Classic rendering can be selected at runtime.

Sprites are read straight from their palette indices. The `sprite_filter`
setting is separate from world smoothing: 0 is nearest (default) and 1 is
bilinear with premultiplied alpha, which avoids color bleed from transparent
pixels.

Enemy floor shadows reuse the current WAD sprite's alpha mask, with soft,
translucent black shading in a separate depth-tested pass that does not write
depth. The flattened silhouette is clipped against convex floor polygons in the
enemy's sector, follows animated frames and moving floors, and fades/spreads
with altitude. Floor bounds reject unrelated polygons before clipping.
`addEnemyShadow` accepts a world-space `ShadowLight` to orient and stretch a
second shadow away from the strongest visible shot or projectile light, while retaining
the contact shadow beneath the enemy.

An optional `R_MuzzleFlash` renderer callback receives player weapon shots,
enemy hitscan shots and enemy missile launches without changing the simulation.
The renderer keeps at most eight transient muzzle sources and the eight nearest glowing
projectiles, with one pulse per shooter and a brief peak followed by a five-tick
fade driven by `leveltime` and render interpolation. Level changes and
save/load discard old pulses. The world fragment shader adds colored brightness
with smooth distance falloff, reaching up to 2.2 times normal texture brightness.
World surfaces also apply a Lambert facing term from their geometric normal,
derived per pixel from screen-space derivatives. Each light carries a
directionality: the flashlight 0.85, shots and projectiles 0.75, and emissive
material sources 0.35 so they still light their own wall. `doom_flash_facing`
in `platform/flash_lighting.h` mirrors the shader formula for data-free checks.
Sprites receive one RGB increment across the billboard, retaining their painted
shading; fully bright WAD frames keep their original colors and white cores.
Accelerated sector lighting omits the classic player's global `extralight` bump;
the software renderer retains it.

Light colors are cached from decoded sprite pixels and the WAD's original
PLAYPAL palette. Bright saturated hue clusters determine the color, excluding
transparent pixels, dark outlines and white cores; neutral art falls back to white.
Player flashes use weapon flash artwork, enemy gunshots use their firing frames,
and enemy missile launches use the emitted projectile's artwork. Rotations are
sampled together so a rocket's hidden exhaust can still supply its light color.
Missile objects with fullbright animation frames emit continuously, following
the same position interpolation as their rendered sprites, including bright
impact animations. Source identity prevents enemies from casting a directional
shadow from their own flash.

Each source collects nearby linedefs for light visibility. Rays to world
fragments and sprite centers stop at solid walls and at the floor/ceiling spans
outside two-sided portals, using current door and lift heights. Masked midtextures
are not sampled for light occlusion. Directional shadows remain floor decals,
clipped within the enemy's sector; they do not project onto walls.
`platform/flash_lighting.h` shares the CPU sampler with data-free checks covering
falloff, solid walls, open/closed doors, raised floors, fade timing and sprite
color selection (green/orange halos, white cores and transparent pixels). The native
smoke test checks player and enemy shot notifications; the 3D version also
runs colored shot and traveling-projectile lights through firing, save/load and
level transitions. Set
`DOOM_CAPTURE_FLASH_TEST=1` for optional before/player/enemy/projectile screenshots in its
isolated working directory.

The native backend currently supports ordinary DOOM maps and original sprite
assets. It does not add modern map formats, slopes, 3D floors, model packs,
ray tracing or multiplayer.

## Engine compatibility

The game code is linuxdoom 1.10 with vanilla behavior, plus what
vanilla-format and limit-removing PWADs need:

- `linuxdoom-1.10/d_deh.c` applies DEHACKED patches: DEHACKED lumps in WAD
  order (skip with `-nodeh`), then `-deh`/`-bex` files. It covers DeHackEd v3
  Thing, Frame, Pointer, Sound, Ammo, Weapon, Cheat, Misc and Text blocks, BEX
  `[STRINGS]`, `[PARS]`, `[CODEPTR]` and `[SPRITES]`/`[SOUNDS]`/`[MUSIC]`
  renames. MBF and MBF21 fields and code pointers are reported and skipped.
  Messages and texts go through `DEH_String`.
- Static limits that aborted or corrupted memory now grow: visplanes,
  drawsegs, vissprites and openings (classic renderer); intercepts, spechit,
  scrolling walls, plats, ceilings, buttons and adjacent-sector searches.
- Map indices are unsigned 16-bit, so maps may have up to 65535 vertexes,
  sides and lines. A missing or oversized BLOCKMAP is rebuilt (`-blockmap`
  forces it); a short REJECT reads as all-visible. Unknown thing types are
  skipped with a warning.
- `tnt.wad` and `plutonia.wad` select Final DOOM's level names and texts.

Demos stay compatible: below vanilla's limits the simulation is unchanged.
Boom/MBF map specials, UMAPINFO and extended node formats are not supported;
maps with XNOD/ZNOD nodes stop with an error naming the format. Mouse look changes the camera while preserving
the original automatic vertical aim. The classic backend remains the reference
for exact original lighting and special rendering tricks.

An optional 3D smoke test requires an active desktop with Metal or Vulkan:

```sh
cmake -S . -B build/play -DDOOM_TEST_GPU=ON
cmake --build build/play --parallel
ctest --test-dir build/play -R gpu-gameplay --output-on-failure
```

It verifies that the accelerated renderer is active, then checks firing,
save/load, and switching to a second level. Leave `DOOM_TEST_GPU` off in
environments without a display. For address checks use `-fsanitize=address`
for C, C++ and Objective-C++ and the executable linker; the combined UBSan
preset is for the data-free checks.

To find slow places in a WAD, `doom-perf-smoke` loads every map and looks
in four directions from spots across it, with the world paused:

```sh
cd build/play && ./doom-perf-smoke /path/to/doom2.wad
./doom-perf-smoke /path/to/doom2.wad -maps MAP15,MAP29 -spacing 256 -width 1920 -height 1080
```

Each frame waits for the GPU, so CPU and GPU times are measured separately.
The slowest views are timed again over several frames. At the slowest one the
renderer leaves out one kind of work at a time (each kind of dynamic light,
reflections, bloom, fog, mist) to show what the time goes to. `perf-report/`
gets `report.txt` (per map: lights at the slowest view with their wall
blockers, the cost of each kind of work, pitfalls such as lights that test
many walls per pixel), `views.csv` (every view) and a screenshot per map.
It reads the player's own `graphics.cfg` unless `-graphics` names another;
`-file`, `-deh`, `-skill`, `-flashlight` and the window options pass through,
`-budget ms` sets the frame budget (default 16.7) and `-strict` fails when a
map exceeds it. Keep the window visible while it runs.

For a bounded frame capture, the app accepts `-rendercheck /absolute/output.png`
(it exits after capturing an actual level). Use `-graphics /absolute/file.cfg`
to isolate graphics settings. These are development options, independent of
normal F7 screenshots.

There is no built-in GPU profiler; use the platform's GPU tools (Xcode
Instruments on macOS) for timing. The window title shows frames per second.

The renderer retains geometry vector capacity, uploads every vertex batch,
blocker and sector record in one copy pass per frame, and shares world vertices
with the bloom pass. SDL_gpu cycles the upload buffers, so the CPU does not wait
for the previous frame. Conservative per-triangle
sphere/bounds masks omit lights that cannot reach a surface. Opaque textures
use early depth tests; the far-plane sky shades only uncovered pixels, followed
by masked surfaces and sprites so transparency keeps its background. Bloom, shadows, light limits and resolution settings retain their quality.
Fog now uses a cheaper atmospheric approximation as described below.

To package a built Mac app with its licenses into the project root, run:

```sh
sh scripts/package-macos.sh
```

The packaging helper uses the SDL license under `build/dependencies` and the
Freedoom license/credits extracted under `build/game-data`. It replaces an
existing app in the project root without keeping a backup. Keep the relevant data
and dependency licenses with any app you redistribute.

The atmospheric fog shader evaluates exponential distance haze directly. The
headlamp uses a closed-form radial falloff integral and a smooth beam angle;
other selected lights use unoccluded ray/sphere glow proxies; walls and doors
block light on surfaces, not in the fog glow. It preserves the original extinction density/range and clear HUD,
but approximates detailed in-air shadows and colored scattering. There is no
six-step volume sampling loop. The existing `fog` config key and `-fog 0/1`
continue to control it. `doom_fog_beam_integral` in `volumetric_fog.h` mirrors
the shader's headlamp formula and is checked against numerical integration.

## Shaders and platforms

Shaders are Vulkan GLSL in `platform/shaders`. `scripts/compile_gpu_shaders.cmake`
compiles them to SPIR-V for Vulkan and translates that to MSL for Metal with
SPIRV-Cross; both are committed in `platform/gpu_shaders.h`, so normal builds
need no shader tools. After a shader change, regenerate it (requires
`glslangValidator` and `spirv-cross`, e.g. `brew install glslang spirv-cross`):

```sh
cmake --build build/play --target doom-gpu-shaders
```

When both tools are installed, the `gpu-shaders-compile` test checks that every
shader still compiles and translates. SDL_gpu orders Metal buffers differently
from SPIR-V, so `world.frag` renumbers its storage buffers under `METAL`.
SDL_gpu cannot sample integer textures, so the light-seam map is uploaded as
16-bit UNORM and converted back exactly in the shader. Without depth resolves,
soft mist under MSAA reads a single-sample depth prepass.

F4 opens the in-game settings panel on every platform. Its options are listed
in `platform/settings_panel.cpp`, grouped into tabs; `platform/settings_menu.cpp`
draws the panel into a 640x400 image in the WAD's own small font (STCFN),
which the renderer scales by whole pixels in the final pass so it stays sharp
at any render resolution. `-gpudebug` enables SDL_gpu validation and `-gpumsaa 1` forces
single-sample rendering. The window title names the active driver.

The panel's Performance tab (both off by default, both need baked static
lights) trades per-frame light work for level-load bakes.
`bake_only_lights 1` takes torches, lamps and glowing textures out of the
dynamic light list. The bake then also records per texel where that light
comes from and which of 15 flicker groups dominates it, so bump detail,
gloss and flicker survive. Lava, nukage, slime and mostly glowing flats light
as one area light per pool instead of a grid of points. The nearest few stay
as fog-only lights for the glow in fog. `grid_sprite_light 1` bakes static
light, its direction and the strongest shadow-casting light every 32 units
at two heights. Things blend that grid instead of tracing rays every frame,
and their floor shadows fall from that light.

`SDL_GPU_DRIVER=vulkan` runs the Vulkan path on a Mac through MoltenVK
(`brew install molten-vk vulkan-loader`):

```sh
SDL_GPU_DRIVER=vulkan SDL_VULKAN_LIBRARY=/opt/homebrew/lib/libvulkan.1.dylib \
  "build/play/Play DOOM.app/Contents/MacOS/Play DOOM"
```

`-rendercheck` captures on E1M1 from the former native Metal renderer, SDL_gpu
with Metal, and SDL_gpu with Vulkan through MoltenVK match to within rounding,
including the flashlight, emissive nukage and HUD. Linux and Windows builds were
cross-compiled but have not yet been run on those systems.

## Publishing a release

`.github/workflows/release.yml` builds the downloads. Push a version tag to
publish them as a GitHub pre-release:

```sh
git tag v0.1.0
git push origin v0.1.0
```

Each download bundles Freedoom Phase 1 (fetched and checksum-verified during the
build) as `default.wad`, plus all licenses. The Mac app is a universal binary
for macOS 11 and later; Windows uses the static MSVC runtime; Linux is built on
Ubuntu 22.04 for older glibc. SDL is linked statically everywhere. Running the
workflow by hand from the Actions tab builds the same downloads as artifacts
without publishing a release.

The release page opens with "What's new": the text of `.github/changes/<tag>.md`
(for example `.github/changes/v0.1.7.md`) when that file is committed before
tagging, otherwise the subject lines of the commits since the previous tag. The
download and first-launch notes from `.github/release-notes.md` follow.

By default the Mac app is only signed ad hoc, and players allow it once under
System Settings → Privacy & Security (the release notes explain how). It is
signed with a Developer ID and notarized instead when these optional repository
secrets are set (Settings → Secrets and variables → Actions):

| Secret | Value |
| --- | --- |
| `MACOS_CERTIFICATE_P12` | Your *Developer ID Application* certificate and private key, exported from Keychain Access as `.p12`, then base64-encoded: `base64 -i cert.p12 \| pbcopy` |
| `MACOS_CERTIFICATE_PASSWORD` | The password chosen when exporting the `.p12` |
| `APPLE_ID` | The Apple ID email of the developer account |
| `APPLE_APP_PASSWORD` | An app-specific password from [account.apple.com](https://account.apple.com) → Sign-In and Security |
| `APPLE_TEAM_ID` | The 10-character Team ID from [developer.apple.com/account](https://developer.apple.com/account) → Membership details |


## Requirements

- CMake 3.21 or newer.
- A C11 compiler: current Apple Clang, Clang, GCC, or Visual Studio 2022 MSVC.
- Ninja when using the supplied presets. Plain CMake commands use your
  platform's default generator and do not require Ninja.

On Windows, use a Visual Studio developer terminal. Apple Silicon builds use
the host architecture automatically; no Intel emulation is required.

## Build and test

From the project root:

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

The `release` preset enables optimization. The `sanitize` preset enables
AddressSanitizer and UndefinedBehaviorSanitizer with GCC/Clang:

```sh
cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize
```

Without Ninja, or with the Visual Studio generator:

```sh
cmake -S . -B build/local -DCMAKE_BUILD_TYPE=Debug
cmake --build build/local --config Debug --parallel
ctest --test-dir build/local -C Debug --output-on-failure
```

Targets:

| Target | Purpose |
| --- | --- |
| `doom_core` | Original engine, compiled as a static library |
| `doom` | Optional playable SDL3 desktop app |
| `doom-native-smoke` | Optional real-level movement, firing and save/load check |
| `doom-gpu-smoke` | Optional 3D-renderer gameplay and level-transition check |
| `doom-perf-smoke` | Optional per-map frame timing and slow-spot report for a WAD |
| `doom-gpu-shaders` | Regenerates `platform/gpu_shaders.h` from `platform/shaders` |
| `doom-headless` | Single-player demo playback without graphics/audio |
| `doom-portability-tests` | Data-free portability regression checks |
| `doom-x11` | Optional historical Linux X11/OSS executable |

The regression checks cover binary record sizes, byte swapping, odd-sized
allocator blocks, purge-owner pointers, numeric/string configuration, player
save-state round trips, and synthetic WAD texture loading. They do not establish
complete gameplay or demo compatibility. Gameplay timing and rendering
are exercised separately by the optional native tests.

## Headless demo playback

Provide a compatible IWAD with `-iwad /absolute/path/to/game.wad`, or set
`DOOMWADDIR` to a directory
containing the appropriate `doom1.wad`, `doom.wad`, `doomu.wad`, or `doom2.wad`
(the original mission-pack filenames are also recognized). Explicit `-iwad`
paths identify the game from its map entries.

Example on macOS/Linux, using an embedded demo and a project-local configuration:

```sh
DOOMWADDIR=/path/to/game-data ./build/debug/doom-headless -playdemo demo1 -config ./build/debug/headless.cfg
```

In a Windows developer PowerShell terminal:

```powershell
$env:DOOMWADDIR = 'C:\path\to\game-data'
.\build\local\Debug\doom-headless.exe -playdemo demo1 -config .\build\local\headless.cfg
```

Run `doom-headless --help` for a brief description. It requires `-playdemo`
so it cannot accidentally enter an interactive loop with no input. The original
`-timedemo` path reports results through a fatal-error exit and is intentionally
rejected here. Demo playback has not yet been verified against original game
data.

## Historical Linux backend

The old platform sources are retained for reference and an optional Linux build:

```sh
cmake -S . -B build/x11 -DDOOM_BUILD_X11=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/x11 --parallel
```

This requires X11/Xext development libraries and Linux OSS sound headers.
At runtime it still expects an 8-bit X11 visual and `/dev/dsp`; it is not a
modern desktop backend. Use the SDL3 backend for modern desktop graphics/audio.
The historical Makefile and auxiliary DOS/sound-server sources are not used by
the modern build.

## Save compatibility

The original save routines copy native structs, whose sizes and alignment change
on modern hosts. Modern saves therefore use a separate `native 110-32` or
`native 110-64` identifier, and pointer-bearing records are padded to the host's
maximum alignment. Original `version 110` saves are rejected. New saves remain
specific to the host ABI/build; the identifier does not promise cross-platform
save compatibility. A portable, versioned serialization format is a later step.

## Continuous integration

`.github/workflows/build.yml` builds and tests Debug and Release configurations
on Linux, Windows/MSVC, Apple Silicon macOS, and Intel macOS. Separate Linux jobs
run the sanitizer checks and compile the historical X11 backend. Native Mac jobs
compile SDL and the 3D renderer on both architectures, as do Linux and
Windows/MSVC jobs; all run only data-free checks. These workflows
run when this project is placed in a GitHub repository; creating the file does
not itself run hosted checks.
