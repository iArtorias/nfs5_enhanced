# Changelog

## 1.4.0 (2026-11-23)

- NFS5 Essentials V2 by ND4SPD Racer is included as a data overlay in the new `GameplayFixes` folder,
  which goes next to `Porsche.exe`. The game reads files under `FEData` and `GameData` from there when
  an override exists, the original files on disk stay untouched. It brings the official addon cars
  911 GT2, 911 GT3, 928 GTS and 959, fixed car textures and palettes, the corrected km/h speedometer
  at night and the Essentials `nfs5.car` and `nfs5.prt`, with their balance, price and tuning changes.
  `GameplayRedirect` in `[IO]` turns it off, `GameplayRedirectTrace` logs every redirected file.
  Details, checksums and credits are in `docs/ESSENTIALS_V2_INTEGRATION.md`.
- New `[CarFixes]` section, fixes to the car and part data applied in memory after loading. Entries
  that a data pack such as `GameplayFixes` already corrected are left alone.
  - `FixCarDatabase`: the '96 911 Carrera 4S (993) pointed at a misspelled sim file and drove with
    `default.sim`. The dash and camera offsets of the '92 928 GTS are restored when an older add-on
    pack overwrote them.
  - `FixPartBugs`: the 996 Stage 4 supercharger had a torque value of 1110 instead of 110 at 7500 RPM
    (about 1065 HP), the 930 Turbo High Performance Exhaust had an all zero torque curve, and the
    Heavy Duty Shocks were a downgrade.
  - `FixPartQuirks`: stray drag on the 356 Street Performance Springs and 356A 1300 engine upgrade,
    the Boxster hardtop cut drag by 20% (now 2%), 944 Bumpers #2 had no downforce, the 993 Extra
    Stiff Sway Bar weight and the grip of the 993 slicks.
  - `FixSuperchargerFlag`: supercharger kits never set the forced induction flag, the game looked for
    the subcategory `Supercharged` while the parts say `Supercharger`.
  - `FixEngineSwaps`: an engine swap updates the redline, and is applied before the engine bolt-ons,
    so the order of the part slots no longer wipes their torque gains.
  - `FixSpringLowering`: the ride height slider of the car setup no longer undoes the lowering of
    the springs.
  - `FixWetTireTable`: slicks lose grip on wet asphalt instead of keeping their dry bonus, and rain
    tires stay at least as good as stock tires on wet dirt, grass and snow.
- Intro and menu movies are enlarged once on the GPU, from the original frame, with a bicubic filter
  (`MovieFilter`, `MovieSharpness` in `[Dx11]`). The game doubled every frame in software and the
  graphics driver stretched the result, which left scan lines. The color of a frame is interpolated
  instead of repeated for two pixels. A movie as wide as the screen fills the window, its black bars
  cut and its aspect ratio kept (`MovieFill`). Needs the DX11 driver, `Upscale` in the new `[Movie]`
  section turns it off. Not tested with `ThrashDriver = dx`, where the game keeps scaling the frames.
- The included ini forces 16x anisotropic filtering (`Anisotropy` in `[Dx11]`), it used to leave it
  to the game.

### Fixes

- The plugin works when Windows loads `Porsche.exe` at another address than `0x00400000`, for example
  with Mandatory ASLR (Exploit Protection, "Force randomization for images") turned on. Before, every
  hook failed with `signature mismatch, found <unreadable>`. Game addresses are rebased at run time and
  signatures ignore operands the loader relocated. The build is still identified by timestamp and image size.
- Interpolation did not install on some copies of `Porsche.exe` because the signature of the
  render call site compared the call target. The target is no longer part of the signature.
- The DX11 driver computed exponential fog from the depth buffer value instead of the distance to
  the eye, so its density did not match the stock driver. New `FogEnabled` in `[Dx11]` turns fog off.
- Projected headlight beams flickered against the road surface with the DX11 driver, they are now
  drawn with a small depth bias.
- Scenery floated in the air with a raised draw distance. One of the two object limits that 1.2.0
  tied to the draw distance keeps its stock value again, and the occlusion range is measured to the
  center of an object instead of its edge.
- The bumper camera of the 911 GT1 had the wrong perspective, patch 3.5 sends that view to the routine
  of another camera. The original routine is restored for the GT1 only, every other car and modified
  car data keep the camera of 3.5.
- A menu scroll bar could stay in drag mode after the mouse button was released outside of it, and
  the list kept following the mouse. This is fixed with `Scroll = 0` as well.
- On mirrored tracks the stereo was swapped, a car passing on the left was heard on the right. The game
  mirrors the listener as well as every car, and the two cancel out. Now only the cars are mirrored,
  new `MirrorPanFix` in the new `[Audio]` section.
- An unsupported `Porsche.exe` no longer has its memory touched when the game exits, and the Thrash
  driver leak fix is applied again when the game reloads the driver.

Thanks to [TheGooselinger](https://github.com/TheGooselinger), [glauberlima](https://github.com/glauberlima),
[L0calH0rst](https://github.com/L0calH0rst) and [properly-strawberry](https://github.com/properly-strawberry)
for the contributions.

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