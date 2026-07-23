# GL Render-Scale + Quality Upscaling — Design

**Date:** 2026-07-22
**Status:** Approved design, pre-implementation
**Scope:** Engine (`keeperfx-linux-alpha`). Linux GL present path only.

## Problem / motivation

In desktop-fullscreen the engine's 8-bit software rasterizer draws the **entire** scene (3D world + GUI/HUD/text) at the full native resolution (e.g. 3440×1440 ≈ 5.0M pixels) every frame, and the GL layer presents it 1:1 with `GL_NEAREST` — already pixel-sharp. There is no upscaling to "fix." The real opportunity is the inverse: let the user **render the scene at a fraction of native resolution** and upscale it well, which is:

1. **A CPU performance lever** — the software rasterizer is the frame-time bottleneck; at 75% it draws ~2.8M px, at 67% ~2.2M px, roughly proportional CPU savings.
2. **A place where quality upscaling finally matters** — a good filter (sharp-bilinear) keeps the downscaled image clean instead of blocky (`GL_NEAREST`) or blurry (`GL_LINEAR`).

Upstream (Windows-first) has no GL present layer at all, so this is fork-only territory.

## Non-goals (v1)

- CRT / scanline post-pass — easy later add (slots into the existing post-FX composite chain); **not** in v1.
- Integer-scale mode — for pixel purists; conflicts with the "modest %" model; **not** in v1.
- Launcher (Qt) UI for the setting — v1 is `keeperfx.cfg`-driven; launcher exposure is a follow-up in the launcher repo.
- **World-only scaling with a native-sharp HUD** — explicitly rejected for v1 (requires splitting the single draw surface into two + GL composite; overlaps the deferred #4 truecolor renderer). v1 scales the **whole scene**; HUD/text softens as scale drops, which is why scale options are capped at readable levels.

## Approach

Whole-scene render-scale: shrink the single 8-bit draw surface (and the GL framebuffer) to `render_scale%` of the mode resolution; keep the window/drawable at native; upscale in the GL present with **in-shader sharp-bilinear** (approach A — palette-lookup each of 4 neighbor index texels, then blend with a sharpened bilinear weight). No extra FBO or pass; degenerates to today's exact 1:1 `NEAREST` at 100%.

### Rejected alternative (upscale technique)
Approach B (render palettized scene to a full-res RGB FBO, then hardware-bilinear upscale in a second pass) was rejected: it adds an FBO + pass to the common no-post-FX path for no quality gain over in-shader sharp-bilinear.

## Components

### 1. Setting — `settings.render_scale`
- New field in `struct GameSettings` (`config_settings.h`), an integer percent.
- Persisted in `keeperfx.cfg` under the graphics settings, following the exact `gamma_correction` pattern (`config_settings.c`: `value_dict_get` load at ~L429, `TOSAVE` write at ~L549, `clamp` at ~L515).
- **Allowed values:** 67, 75, 85, 100. Clamp/snap anything else to the nearest allowed value. **Default 100** — byte-identical to current behavior.

### 2. Surface sizing — `bflib_video.c` `LbScreenSetup`
- Compute `render_w = round(mdinfo->Width * render_scale / 100)`, `render_h = round(mdinfo->Height * render_scale / 100)`.
- Create the GL draw surface (`SDL_CreateRGBSurface`, ~L664) and call `gl_present_init(lbWindow, render_w, render_h)` at the **scaled** size. The window/drawable is unchanged (still native).
- Set `lbDisplay.PhysicalScreenWidth/Height` (~L703) to the **scaled** dims (they currently take `mdinfo->Width/Height`). This is what makes the rest of the engine treat the scaled surface as "the screen."
- The CPU fallback path (non-GL) is **unchanged** — render-scale is GL-present only. When `!lbUseGLPresent`, force `render_scale = 100` internally.

### 3. Upscale filter — `bflib_render_gl.c` `gl_run_scene_pass` shader
- Replace the single `texture(u_indexed).r → palette` sample (shader at ~L201–206) with a sharp-bilinear resolve: compute the source texel coord from `v_uv` and the framebuffer size (add a `u_fb_size`/`u_texel` uniform), sample the 4 surrounding index texels with `texelFetch` (NEAREST), palette-look-up each to RGB, and blend using bilinear weights where the fractional coordinate is passed through a "sharpen" ramp (`clamp((f - 0.5)/fwidth + 0.5, 0, 1)` style) so pixel edges stay crisp and shimmer-free.
- Guard: when framebuffer size == viewport size (100% / 1:1) the resolve must produce the **identical** result to today's NEAREST path (verify: at integer 1:1 the 4-tap weights collapse to the single covering texel).
- Same treatment applied wherever the scene pass shader is used for the direct and scene-FBO paths, so post-FX and non-post-FX both get it.

### 4. Input + GUI mapping
- **GUI/text:** derives from `MyScreenWidth = width * psize` (`vidmode.c:726`) and `LbScreenSetGraphicsWindow(..., MyScreenWidth/pixel_size, ...)` (`vidmode.c:759`). Because `setup_screen_mode` receives the **scaled** surface dims, the GUI re-lays-out proportionally with no extra code — **to be verified**, not assumed.
- **Mouse:** the host-cursor ↔ game-cursor mapping (`bflib_mouse.cpp:125–140`) and `SetMouseWindow(x,y,width,height)` (`bflib_mouse.cpp:200`) operate in surface space. With a scaled surface, the SDL host cursor (window space) must map to the scaled surface space by the surface/window ratio (the same ratio the GL present letterbox already uses). This is **the primary correctness risk** and gets explicit test coverage.

## Data flow

```
keeperfx.cfg [graphics] render_scale
   -> settings.render_scale (clamped)
   -> LbScreenSetup: render_w/h = native * scale%; scaled draw surface + gl_present_init(render_w,h)
   -> software renderer draws world+HUD into the scaled 8-bit surface
   -> gl_present_frame: sharp-bilinear upscale of the scaled framebuffer to the native drawable
   -> SDL_GL_SwapWindow
```

## Error handling / edge cases

- Invalid/absent `render_scale` in cfg → default 100.
- Non-GL (CPU blit) path → forced 100 (no scaling).
- Mode/resolution switch at runtime (existing vidmode switching) → recompute scaled dims and re-init the surface + GL fb, same as any mode change.
- Movie playback (`gl_present_frame_rgba`) is unaffected — it has its own path and native-res RGBA source.

## Testing

- **Builds clean; 100% is byte-identical** to pre-change behavior (the critical regression guard — default users see zero change).
- **Headless boot** at each scale (67/75/85/100) via the xvfb `-level N` harness: reaches main menu + loads a level, no crash, no GL errors.
- **HUD/GUI layout** at each scale: status panel, tooltips, and message text render in the correct positions and are readable (visual check).
- **Mouse accuracy** at each scale: cursor position and click-picking (select a creature, click a room button) land correctly — **user playtest**, the one thing the headless harness can't confirm.
- **Performance**: capture a rough frame-time delta 100% vs 67% to confirm the CPU win is real (not just theoretical).

## Rollout

Ship behind the default-100 setting (opt-in). Once validated in-game, document in the README "Linux performance" section as a user-selectable performance/quality option. Launcher UI + CRT + integer-scale are tracked as follow-ups.
