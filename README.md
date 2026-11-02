<p align="left">
  <img src="images/logo.png" alt="NFS5 Enhanced" width="10%">
</p>

---

# NFS5 Enhanced - Need for Speed: Porsche Unleashed Enhanced Patch

[![GitHub All Releases](https://img.shields.io/github/downloads/iArtorias/nfs5_enhanced/total.svg)](https://github.com/iArtorias/nfs5_enhanced/releases)

> The game you remember, running the way it always should have: wide on any screen,
> smooth at any refresh rate, stable on current Windows, and without touching a single game file.

> [!IMPORTANT]
> Tested on the **USA** release with the **official v3.5 patch** (`Porsche.exe` with
> MD5 `316F33A59DB48A7DB2A75CC4B3BD03DC`). Any other executable is detected at startup and nothing is hooked,
> so the game runs untouched instead of crashing.

> [!NOTE]
> NFS5 Enhanced is an ASI plugin with its own graphics driver. `Porsche.exe` and the game data are
> never modified on disk, and removing three files restores the original game.

---

## Overview

**NFS5 Enhanced** is an *enhanced vanilla* patch. It does not add cars, tracks, physics changes
or new visuals. Everything the game shows is still drawn by the game itself, with its own models,
textures, HUD art and fonts. What changes is everything around it: the projection is widened
instead of cropped, the HUD keeps its 640x480 proportions at FullHD, 2K, 4K, 8K, motion is
interpolated between simulation steps, distant scenery and cars stay on screen, and a long list
of crashes and Windows 10/11 incompatibilities is gone.

The game draws through a small graphics driver dynamic library of its own, the stock one is built on DirectX 7.
NFS5 Enhanced ships a replacement driver, `dx11z.dll`, that renders the same game through
Direct3D 11 and is used by default.

It also contains a full rewrite of the NFS Porsche fixes from Silent's [SilentPatchNFS90s](https://github.com/CookiePLMonster/SilentPatchNFS90s), so
that patch is no longer needed alongside it.

Every option lives in one file, `nfs5_enhanced.ini`, and every feature can be switched off on its
own.

---

## Screenshots

<table>
  <tr>
    <td align="center"><b>Quick Race</b><br><a href="images/screenshot-quick-race.png"><img src="images/screenshot-quick-race.png" alt="Quick Race mode" width="50%"></a></td>
    <td align="center"><b>Evolution</b><br><a href="images/screenshot-evolution.png"><img src="images/screenshot-evolution.png" alt="Evolution mode" width="50%"></a></td>
  </tr>
  <tr>
    <td align="center"><b>Factory Driver</b><br><a href="images/screenshot-factory-driver.png"><img src="images/screenshot-factory-driver.png" alt="Factory Driver mode" width="50%"></a></td>
    <td align="center"><b>Multiplayer</b><br><a href="images/screenshot-multiplayer.png"><img src="images/screenshot-multiplayer.png" alt="Multiplayer" width="50%"></a></td>
  </tr>
</table>

## Features

### Direct3D 11 renderer

The game talks to its graphics driver through a fixed interface (THRASH). `dx11z.dll` implements
that interface on Direct3D 11 and replaces the DirectX 7 `dx7z.dll`, so no DirectDraw wrapper is
needed any more. It emulates exactly what the stock driver did, the game itself is not changed.

| Feature | What it does |
|---|---|
| Sharp garage and showroom | The garage and showroom are rendered at the size they cover on the display and stay at 4:3, races are rendered at their own resolution. |
| Antialiasing | Multisample antialiasing up to 8x, and optional anisotropic texture filtering. |
| Window modes | Borderless window over the whole monitor, exclusive fullscreen at the desktop resolution, or a resizable window. The display never switches to 640x480. |
| VSync | `On` or `Off`. `Off` presents frames at once, with tearing where the display allows it. |
| Alt+Tab | The game minimizes when another program is activated and comes back on return. The game no longer pulls itself back in front. |
| Movies | The intro and menu movies are enlarged once on the GPU with a bicubic filter, from the original frame, instead of being doubled in software by the game. This removes the scan lines of the original, and a movie as wide as the screen fills the window with its black bars cut and its aspect ratio kept. `[Movie]` and `MovieFilter`, `MovieSharpness`, `MovieFill` under `[Dx11]` switch each part off. |
| High DPI | The window is DPI aware, so the picture is not stretched by Windows scaling. |
| Clean exit | The device is released before the game unloads the driver, so the process always ends. |
| Fast draw path | Repeated render states no longer split batches, geometry is streamed through ring buffers and pipeline objects are only rebound when they change. The sun glare test copies only the pixels it needs instead of scaling the whole frame down first. |
| Fallback | `ThrashDriver = dx` in `nfs5_enhanced.ini` switches back to the stock `dx7z.dll` at any time. |

### Controllers

Gamepads are read through [SDL3](https://libsdl.org), the game never talks to them directly.

| Feature | What it does |
|---|---|
| Any gamepad | DualSense, DualShock 4, Xbox, Switch Pro and every other pad SDL knows work as they are, no DS4Windows or other converter needed. |
| Hotplug | Pads can be connected and disconnected at any time, also in the middle of a race. The pad pressed last is the one in use. |
| Bindings that stay | The game sees one controller, always present and always at the same place in its list, so bindings made in *Options > Controller* survive restarts and pad swaps. |
| Modern default bindings | Right trigger accelerates, left trigger brakes, left stick steers, A hand brake, B camera, X look behind, Y reset car, right and left shoulder shift up and down. Applied when the gamepad is picked as default controller in *Options > Controller*. Every stick, trigger and button can still be bound freely. |
| Rumble | The force feedback the game plays for wheels drives the pad motors. |
| Scrolling menu lists | Car, paint, track and other lists in the menus scroll with the mouse wheel over the list, and with the arrow keys, Page Up / Page Down and Home / End. The game only moved them by dragging the scroll bar. The wheel over a closed drop down list picks the next or previous entry. |

### Widescreen

| Feature | What it does |
|---|---|
| Hor+ projection | Every 3D camera, including the rear view mirror, keeps the 4:3 vertical view and opens up to the sides. Nothing is cropped or stretched. |
| Round light flares | Headlight and lamp flares stay round on wide screens instead of being stretched. |
| Matching culling | Sky and track chunks are selected with the same widened field of view as the picture. The game used to test them against the narrow 4:3 view even with Hor+ on, so ground and sky near the screen edges could be missing on wide monitors. |
| Unlocked resolutions | Every display mode of 640x480 and up is offered. The game used to hide modes behind a video memory estimate from 2000. |
| Largest mode | The resolution list skipped the last entry of the driver mode table, usually the native resolution in 32 bit, so it was only offered in 16 bit. |
| Race resolution | Races can use the Options resolution, the desktop resolution or any fixed size, in 16 or 32 bit color. |

### HUD

| Feature | What it does |
|---|---|
| HUD scaling | The race HUD and the in race menus grow with the screen height, so they keep the size they had at 640x480. Each element stays anchored to its own screen edge. |
| Text scaling | HUD text is scaled together with the rest of the HUD. |
| Options that apply | Speedometer, tachometer and the other *Options > Display* settings now apply in races at every resolution. The game edited the layout of one resolution and raced with another. |
| Analog or digital | A gauge set to analog shows only the gauge, a gauge set to digital shows only the number. The original drew the digital value inside the analog gauge too. |
| Options preview | The HUD preview in the options menu is scaled to match. |
| Sharp text | Every font is a small 4 bit glyph sheet made for 640x480. Each sheet is redrawn at 2 or 4 times its resolution with smooth edges, so menu and HUD text stays crisp at 1440p and 4K. Same typeface, same layout, same colors. |
| Loading screen | The race loading screen and its progress bar fill the screen height instead of a small 640x480 box, optionally smoothed. |

### Smoothness

| Feature | What it does |
|---|---|
| Interpolation | The simulation runs at a fixed 64 steps per second. Car and camera positions and rotations are blended between steps, so motion is smooth at 120, 144 Hz and beyond. Teleports, resets and camera cuts are shown at once, never blended. |
| Frame limiter | Separate frame caps for races and for menus, paced with a high resolution waitable timer. |
| Precise game clock | The 128 Hz `timeSetEvent` game clock is replaced with a `QueryPerformanceCounter` scheduler that does not drift or stutter. |

### Draw distance and detail

| Feature | What it does |
|---|---|
| Draw distance | Track scenery is drawn twice as far, on top of the *View Distance* option. `MinDrawDistance` draws at least a fixed distance everywhere. |
| Consistent scenery | Two kinds of scenery had fixed limits of 600 and 700 m that ignored the draw distance. With a longer distance they disappeared while everything around them was still drawn, buildings floated and ground went missing. They now follow the draw distance. |
| Far clip | The 700 unit far plane that cut off cars and objects follows the new draw distance, with room for large pieces of scenery so they are not sliced at the horizon. |
| No nearby holes | Ground close to the car no longer disappears behind the track's occlusion walls, tested in the top view without height, which left holes just outside a slightly raised spot. `OcclusionRange` (default 220m) sets how close, `0` restores the original test. |
| Car detail | Cars keep their detailed models 4 times further. The wider Hor+ view is compensated automatically. |
| Full car detail | The game never drew the most detailed car model in races, not even for your own car, so cars looked simpler than in the garage. Nearby cars now use the same model the garage shows, and keep their wheel suspension travel, which the game only applied at lower LODs and used to leave fixed to the rolling body. |
| Full texture quality | The game rates any CPU above 2.1 GHz as a 1990s machine and loads every texture in 16 bit, the larger ones at half size, so car lettering, plates and decals look blurry next to the garage. Textures now load at the highest quality. |
| Distant wheels | The two farthest car models have no wheels in the game data, cars in the distance now keep theirs. |
| Lamp glow | Headlight and tail light glow sprites no longer turn black past about 90m, where their depth bias used to round down to 0 and z-fight with the lamp lens. This also happened in the stock game. |

### Stability and compatibility

| Fix | What it does |
|---|---|
| No CD needed | A full install starts without the game disc. The game looked for its CD and asked for it in a loop when none was found, the disc check is skipped in memory. |
| Windows 10 startup crash | Only attached keyboards and controllers are enumerated. This also makes the controller rebinding screen fast again. |
| "DirectX 0" error | The game picks its graphics driver from the registry and claims DirectX 0 is installed when that driver cannot load, which often happens after some widescreen mods. A driver that fails to load is reported as the stock `dx7z.dll` instead. The registry itself is never written. |
| Crash after a race | `gimme.dll` read past its buffer while converting the driver picture on the way back to the menu. |
| Sun glare crash | The sun glare test read pixels outside the frame when the sun sat at the screen edge. |
| Alt+Tab | All keys are released when the window loses focus, so no more stuck throttle. |
| Alt+F4 | Closes the game again, a plain Alt still cannot stall it. |
| Lock keys | `Caps Lock`, `Num Lock` and `Scroll Lock` are no longer forced off at startup. |
| Clipboard | `Ctrl+V` pastes into name and chat boxes. |
| CPU affinity | Only the game's own threads are pinned to one core. Sound, input and presentation run freely instead of sharing a core with the game. |
| OpenGL driver leak | Memory leak fix for the Verok OpenGL Thrash drivers |

### Loading

| Feature | What it does |
|---|---|
| CPU clock check | The game measured the CPU clock for a full second on every video mode change, profile load and race start. It is now measured once and reused. |
| Larger reads | Game archives are read in 1 MB chunks instead of 16 KB. |
| Free space cache | The free disk space check that ran on every file open is cached for two seconds. |

---

## Requirements

- Windows 10 or 11
- **Need for Speed: Porsche Unleashed**, USA release plus the official **v3.5** patch applied over
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) installed as
  `dinput.dll` in the game folder
- [DDrawCompat](https://github.com/narzoul/DDrawCompat) (`ddraw.dll`) is only needed with the stock
  DirectX 7 driver (`ThrashDriver = dx`).
- [SDL3](https://github.com/libsdl-org/SDL) (`SDL3.dll`) for controller support. Without it the game keeps its own
  DirectInput controller handling.

---

## Installation

### 1. Get the files

Get the latest release from the [Releases](https://github.com/iArtorias/nfs5_enhanced/releases) page and unzip it anywhere.
*The original `Porsche.exe` from the official **v3.5** patch is included.*

### 2. Copy them into the game folder

Copy `nfs5_enhanced.asi`, `nfs5_enhanced.ini` and `SDL3.dll` next to `Porsche.exe`, and `dx11z.dll` into the
`Drivers` folder of the game, next to `dx7z.dll`. If you use DDrawCompat with the stock driver, back
up your `DDrawCompat.ini` and copy the included one over it.

### 3. Remove SilentPatchNFS90s

If `SilentPatchNFS90s.asi` is in the game folder, rename it to `SilentPatchNFS90s.asi.disabled`.
NFS5 Enhanced contains all of its NFS Porsche fixes. While both are present the fixes it shares
with SilentPatch are skipped, so the two never patch the same code.

### 4. Play

Start the game as usual and pick a resolution in *Options > Graphics*. Menus stay at 4:3, races
use the wide resolution. The registry value that selects the game's driver is never written, the
choice is made in `nfs5_enhanced.ini` every time the game starts.

---

## Configuration

Everything is set in `nfs5_enhanced.ini`, and every setting is explained in the file. The sections:

| Section | Contents |
|---|---|
| `[General]` | Log level, coexistence with `SilentPatchNFS90s`. |
| `[Compat]` | The SilentPatch fixes, the crash fixes, graphics driver selection and fallback. |
| `[Dx11]` | Internal resolution, antialiasing, anisotropic filtering, window mode and size, VSync, Alt+Tab behaviour, movie filter and fill. |
| `[Affinity]` | Which threads are pinned and to which core. |
| `[Timing]` | Precise game clock. |
| `[Widescreen]` | Hor+, field of view, flares, resolution list, race resolution and color depth, car detail distance. |
| `[Framerate]` | Frame caps, interpolation. |
| `[IO]`, `[Loading]` | Read chunk size, free space cache, CPU clock check. |
| `[HudScaling]` | HUD size, text, gauge style, loading screen. |
| `[Text]` | Sharp text, enlargement factor and edge sharpness. |
| `[Detail]` | Draw distance, far clip, distant wheels, occlusion range. |
| `[Controller]` | SDL3 gamepads, rumble, dead zones, default bindings. |
| `[Menu]` | Mouse wheel and key scrolling of the menu lists. |
| `[Movie]` | Hands the movie frames to the DX11 driver, which enlarges them. |

Common adjustments:

| I want | Set |
|---|---|
| The original field of view on a wide screen | `HorPlus = 0` |
| A wider view | `FovMultiplier = 1.1` |
| Races at the desktop resolution | `RaceResolution = desktop` |
| A smaller or larger HUD | `ScaleMultiplier = 0.8` or `1.2` |
| Softer or crisper text edges | `Sharpness = 0.3` or `1.2` |
| The original draw distance | `DrawDistanceScale = 1.0` |
| A frame cap of 144 in races | `FpsLimit = 144` |
| A window instead of fullscreen | `WindowMode = 2` |
| No VSync | `VSync = 0` |
| 8x antialiasing | `Antialiasing = 8` |
| The stock DirectX 7 driver | `ThrashDriver = dx` |
| Scenery even further away | `DrawDistanceScale = 4.0`, or a fixed `MinDrawDistance = 2000` |
| Stronger or weaker rumble | `RumbleStrength = 1.5` or `0.5` |
| The game's own DirectInput controller handling | `Enabled = 0` under `[Controller]` |
| The original occlusion test near walls | `OcclusionRange = 0` under `[Detail]` |
| No mouse wheel scrolling in menu lists | `Scroll = 0` under `[Menu]` |
| Movies as the game scales them | `Upscale = 0` under `[Movie]` |
| Movies in 4:3, without filling the window | `MovieFill = 0` under `[Dx11]` |
| Crisper or softer movies | `MovieSharpness = 50` or `20` under `[Dx11]` |

> [!TIP]
> With interpolation on, the race draws as often as it can. Cap it with `FpsLimit` or with `VSync`
> in `[Dx11]` or `DDrawCompat.ini`.

### DDrawCompat changes

Only used with the stock driver (`ThrashDriver = dx`). The included `DDrawCompat.ini` changes these settings, each marked with a comment in the file:

| Setting | Value | Why |
|---|---|---|
| `CpuAffinity` | `app` | A process wide pin also trapped DirectSound, DirectInput and the presentation thread on one core. NFS5 Enhanced pins only the game's threads. |
| `CpuAffinityRotation` | `off` | Rotation rewrites the process mask and undoes the per thread pinning. |
| `Antialiasing` | `msaa4x` | Better antialiasing. |
| `ResolutionScale` | `display(1)` | The 640x480 menus, garage and showroom are rendered at full display resolution. |
| `SpriteDetection`, `SpriteFilter`, `SpriteTexCoord` | `zconst`, `point`, `round` | Menu text and HUD art stay sharp while 3D keeps normal filtering. |
| `SupportedResolutions` | 640x480 up to 4K | The game's driver keeps only about 30 modes. This list makes sure the wide ones are among them. You can edit the list on your own. |

---

## Troubleshooting

<details>
<summary><b>"DirectX 0" error at startup</b></summary>

Check that `Drivers\dx11z.dll` exists in the game folder. When the selected driver cannot be
loaded the stock `dx7z.dll` is used instead. `ThrashDriver = dx` always uses the stock driver,
`ThrashDriver = auto` keeps whatever the registry names.

</details>

<details>
<summary><b>Black screen or wrong picture with the Direct3D 11 driver</b></summary>

`Drivers\dx11z.log` lists the device, the window and every video mode change. Include it in a bug
report. `Trace = 3000` in `[Dx11]` adds a call trace to the log. Until it is fixed, `ThrashDriver = dx`
switches back to the stock driver.

</details>

<details>
<summary><b>The race resolution does not change</b></summary>

The stock graphics driver keeps only about 30 display modes. If the requested one is not among
them, the game's own choice is kept and the log says so. Add the resolution to
`SupportedResolutions` in `DDrawCompat.ini`. The Direct3D 11 driver lists every mode of the display.

</details>

<details>
<summary><b>The HUD is too large or too small</b></summary>

Change `ScaleMultiplier`. `ReferenceHeight` sets the screen height at which the HUD
has its original size.

</details>

<details>
<summary><b>Frame rate drops on some tracks</b></summary>

The longer draw distance asks a lot of an engine from 2000 on wide open tracks. Lower
`DrawDistanceScale` to `1.5`, or `1.0` for the original distance.

</details>

<details>
<summary><b>The controller does nothing</b></summary>

Check that `SDL3.dll` is next to the ASI and that `nfs5_enhanced.log` lists the pad under
"Gamepad connected". Then bind the controls once in *Options > Controller*, the controller is
named after the pad. Tools that turn a pad into a virtual Xbox pad are not needed, with them
running the pad in use is simply the one pressed last.

</details>

<details>
<summary><b>Nothing seems to change</b></summary>

Check that `nfs5_enhanced.log` appears next to the ASI after starting the game. If it does not, the
ASI loader is missing. If it does, the log names any hook that did not match the executable, which
usually means a different game version.

</details>

<details>
<summary><b>The game crashed</b></summary>

`nfs5_enhanced.log` ends with the crash location and `nfs5_enhanced_crashdump.dmp` is written next to it. Include
both in a bug report.

</details>

---

## Building

Visual Studio 2022/2026 with CMake 3.24 or newer:

```bat
cmake -S . -B build -A Win32
cmake --build build --config Release
```

MinGW i686 works too, using the toolchain file in `cmake/`:

```bat
cmake -S . -B build-mingw -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-mingw
```

[SafetyHook](https://github.com/cursey/safetyhook) and [Zydis](https://github.com/zyantific/zydis)
are vendored as amalgamated sources under `external/`. The [SDL3](https://github.com/libsdl-org/SDL)
headers and the official 32 bit `SDL3.dll` are under `external/SDL3`, the ASI loads the library
at run time and does not link against it. The build copies `SDL3.dll` next to the ASI.

---

## Credits

- [Silent](https://github.com/CookiePLMonster) for [SilentPatchNFS90s](https://github.com/CookiePLMonster/SilentPatchNFS90s), whose NFS Porsche fixes were rewritten here
- [narzoul](https://github.com/narzoul) for [DDrawCompat](https://github.com/narzoul/DDrawCompat)
- [ThirteenAG](https://github.com/ThirteenAG) for the [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
- The [SDL](https://libsdl.org) developers for SDL3