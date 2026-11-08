# Renderers

| Renderer setting | Status in 13 Sentinels | Backend |
|---|---|---|
| Vulkan (default) | Supported; the renderer the game was verified with | `video_core/renderer_vulkan` |
| Auto | Vulkan, or OpenGL when this system cannot run Vulkan | chosen at start |
| OpenGL (experimental) | Not yet verified in this game | `video_core/renderer_opengl`, desktop OpenGL 3.3 core |
| Direct3D 11 (experimental, Windows) | Not yet verified in this game | the same OpenGL backend on OpenGL ES 3.0, translated by ANGLE |

The setting is `renderer` in `tsr.ini` (`vulkan`, `auto`, `opengl`, `d3d11`). The launcher offers it on the Display page
and the in-game Options menu (F1) on its Graphics page; both save it, and it takes effect the next time the game starts.
An unknown value starts Vulkan. Only Auto falls back: OpenGL and Direct3D 11 chosen by name start as themselves or stop with
the reason, and Play then checks Vulkan only for Vulkan and Auto.

## Where the code comes from

The OpenGL and Direct3D 11 backends are the Dragon's Crown Pro PC port's, unchanged apart from names: the presenter,
rasterizer, shader cache and GLSL emitter (`Desktop330` and `Gles300` targets), texture cache and CPU detiler, guest write
watch, stream ring, state tracker, ANGLE loading (`gl_angle.cpp`) and the overlay's GLES path. ANGLE itself is pinned in
`gpu/angle-runtime.txt`, fetched, verified and staged beside the game by `tools\build.ps1`, and packaged by
`tools\package.ps1`; nothing links it.

What 13 Sentinels adds does not depend on the renderer:

- Videodec2 (the game's H.264 movies) decodes with FFmpeg into guest memory; the frames reach the screen through the
  ordinary texture path of whichever renderer runs.
- The Dragon's Crown Pro workarounds that exist only in its Vulkan path (AMD water fix, narrow water reader skip,
  ultrawide side mask) are not part of this port, so the OpenGL path is missing nothing the Vulkan path has here.

## Limitations carried over

- Indexed rectangle lists are skipped and logged on the OpenGL ES (Direct3D 11) path; instanced rectangle lists draw their
  first instance; a rectangle list reading more than five vertex attributes is skipped; 1D images are refused.
- Direct3D 11 needs feature level 10_1 (OpenGL ES 3.0 with per-target blending); BC6 and BC7 need 11_0 and draw black
  below it.
- In Dragon's Crown Pro an early run of the shared backend once stopped without an error; a silent stop or hang remains
  possible on both experimental renderers.

## Checks

`.\tools\test.ps1 -Group Video` runs the headless checks: the renderer setting and its fallback, the OpenGL and OpenGL ES
requirements, the launcher and Options menu agreeing with the video core, the detiler against PS4 tiler addresses, the
emitted GLSL compiled by glslangValidator for both targets, the write watch against real page faults, and the ANGLE pin,
cache, staging and package gate. None of them creates a window or a graphics context.

They do not show that the game renders correctly with OpenGL or Direct3D 11. That needs a manual run: choose the renderer
in the launcher, start the game, and look in the log for `Renderer: selected ..., active ...`, `OpenGL: vendor ...,
renderer ...` (ANGLE names `Direct3D11` there) and `OpenGL: first guest frame presented`, then check the title screen, a
movie, the Destruction (battle) mode and a save load.
