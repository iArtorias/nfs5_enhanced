# Changelog

## Unreleased

### Fixes

- The plugin works when Windows loads `Porsche.exe` at another address than `0x00400000`, for example
  with Mandatory ASLR (Exploit Protection, "Force randomization for images") turned on. Before, every
  hook failed with `signature mismatch, found <unreadable>`. Game addresses are rebased at run time and
  signatures ignore operands the loader relocated. The build is still identified by timestamp and image size.
- Interpolation did not install on some copies of `Porsche.exe` because the signature of the
  render call site compared the call target. The target is no longer part of the signature.

## 1.3.0 (2026-10-23)

- Menu lists (cars, paints, tracks and others) scroll with the mouse wheel over the list, and with
  the arrow keys, `Page Up`, `Page Down` and `Home`, `End`. The game only moved them by dragging the scroll
  bar. New `[Menu]` section: `Scroll`, `WheelRows` (rows per wheel notch, default `3`) and
  `WheelSelect` (the wheel over a closed drop down list picks the next or previous entry).

### Fixes

- Sky and track chunks were culled with the narrow 4:3 field of view even with `HorPlus` on, so
  ground and sky near the screen edges could be missing on wide monitors. Culling now uses the
  same widened field of view as the picture.
- Ground pieces close to the car were dropped by the occlusion test against the track's walls.
  That test runs in the top view without height, so from a slightly raised spot nearby ground was
  hidden although it is in plain sight. New `OcclusionRange` (`[Detail]`, default 220m) keeps
  objects closer than that drawn. `0` restores the original test everywhere.
- Wheels never received their suspension travel at LOD 0, the model `FullCarDetail` selects, and
  stayed fixed to the rolling body instead, sinking into the ground on the outside of a bend and
  lifting on the inside.
- Lamp glow sprites (headlights, tail lights) round their depth bias to 0 beyond about 90m and
  z-fight with the lamp lens of the car model, turning part of the glow black; this also happens
  in the stock game.

  Thanks to [L0calH0rst](https://github.com/L0calH0rst) for the contribution.

## 1.2.0 (2026-10-19)

### Controllers

- New `[Controller]` section: gamepads through SDL3 (`SDL3.dll`, loaded at run time). DualSense,
  DualShock 4, Xbox, Switch Pro and every other pad SDL knows work without DS4Windows or similar.
- Pads can be connected and disconnected at any time, the one pressed last is used.
- The game sees a single controller that is always present and listed right after the keyboard,
  so bindings are no longer lost between starts. The game reset all bindings whenever the
  controller a binding pointed at was missing or listed at another place.
- DirectInput copies of pads SDL reads are left out of the list (`HideHandledDevices`).
- Modern default bindings for the gamepad (`TriggerBindings`): right trigger accelerate, left
  trigger brake, left stick steer, A hand brake, B camera, X look behind, Y reset car, shoulders
  shift up and down.
- Force feedback is played as rumble (`Rumble`, `RumbleStrength`).

### Direct3D 11 renderer

- Much less CPU time per frame: a repeated render state no longer ends the batch, vertices and
  indices go through ring buffers instead of a buffer discard per draw, and constants, states,
  textures and samplers are only rebound when they change.
- The sun glare probe copies only the 4x4 pixels it reads, it used to scale the whole frame, up to
  4K with 4x MSAA, down to the game size five times a second.
- The sun glare probe no longer waits for the GPU. It gets the pixels of its previous read, 200 ms
  earlier. The game turns the lens flare off for the race once these reads average more than
  5 ms, which waiting for a busy GPU easily took.
- Partial clears no longer create a blend state each time.
- Texture handles are looked up instead of read, a stale handle from the game no longer touches
  freed memory, and freeing textures no longer searches the whole list.

### Fixes

- The resolution list in *Options > Graphics* skipped the last mode of the driver table, which is
  the largest display mode in 32 bit, for example 2560x1600x32 on a 2560x1600 display.
- Without saved options the screen size started at 640x480x16, it now starts at the desktop size
  in 32 bit.
- The screen size is saved as the mode itself. The game kept its position in the list, which
  pointed at another mode as soon as the list changed, after a monitor or driver change or with
  `UnlockResolutions`. Positions in older saves are still read.
- The Direct3D 11 driver lists the display modes sorted by size and color depth.
- Distant scenery is drawn consistently. Two kinds of scenery objects had fixed limits of 600 and
  700 m that ignored the draw distance, so with `DrawDistanceScale` above 1 they vanished while
  the rest was still drawn. Buildings floated in the sky and ground was missing under them. Both
  limits now follow the draw distance.
- The far clip plane leaves room for large pieces of scenery, they were sliced at the horizon
  and flickered in and out as single triangles.
- New `MinDrawDistance` draws at least a fixed distance at every spot of the track, the track
  data keeps it short in many places for the hardware of 2000s.
- `DrawDistanceScale` defaults to 2, the included ini used 10.
- `ThrashDriver` and `LodDistanceScale` had different defaults in code and documentation.
- The Thrash Driver registry override could write a longer name than the buffer of the game held.

## 1.1.0 (2026-10-13)

### Direct3D 11 renderer

- New graphics driver `dx11z.dll`, a Direct3D 11 implementation of the game's THRASH driver
  interface that replaces the DirectX 7 `dx7z.dll`. It is now the default (`ThrashDriver = dx11`),
  `ThrashDriver = dx` returns to the stock driver.
- The fixed function pipeline of the original driver is emulated in shaders: two texture stages
  with all combine modes the game uses, specular, alpha test, vertex and table fog, the depth bias
  and flat shading quirks of `dx7z.dll`.
- The 640x480 garage and showroom are rendered at the size they cover on the display, races
  at their own resolution. Previously this needed DDrawCompat.
- Multisample antialiasing (`Antialiasing`, up to 8x) and optional forced anisotropic filtering
  (`Anisotropy`).
- Window modes: borderless, exclusive fullscreen at the desktop resolution, or a resizable window
  with a configurable size (`WindowMode`, `WindowWidth`, `WindowHeight`). The display mode is never
  changed to 640x480.
- `VSync` on or off, with tearing support where the system allows it.
- Alt+Tab: the full screen modes minimize when another program is activated and come back on
  return (`MinimizeOnFocusLoss`). The game's own focus grabbing, which pulled it back in front right
  after Alt+Tab, is ignored while another program is active.
- Intro and menu movies play, through the same front buffer path the game uses with the stock
  driver, in 16 bit menus as well as 32 bit modes.
- The game brightness option is applied on present instead of through the display gamma ramp.
- The window is per monitor DPI aware, so Windows scaling does not stretch the picture.
- Every display mode of the monitor is offered, the stock driver kept only about 30.
- The device is released when the game shuts the driver down, so the process always exits.
- `dx11z.log` next to the driver records the device, window and video mode changes. `Trace` in
  `[Dx11]` adds a call trace for bug reports.
- New `[Dx11]` section in `nfs5_enhanced.ini` with all of the above.

### Other

- DDrawCompat is now optional and only needed with `ThrashDriver = dx`.

## 1.0.0 (2026-10-12)

First release.