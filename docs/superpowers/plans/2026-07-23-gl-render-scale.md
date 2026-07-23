# GL Render-Scale + Quality Upscaling — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a user-selectable internal render-resolution (`render_scale`: 67/75/85/100%, default 100) that shrinks the 8-bit software render surface for a CPU performance win, upscaled to the native window with in-shader sharp-bilinear.

**Architecture:** Shrink `lbDrawSurface` + the GL framebuffer to `render_scale%` of the mode resolution; keep the window/drawable native. The software rasterizer draws fewer pixels; `gl_run_scene_pass` upscales the smaller framebuffer with a sharp-bilinear resolve (palette-lookup 4 neighbor index texels, blend with a sharpened weight). GUI scaling follows automatically via `setup_screen_mode`; the mouse window→surface transform must be applied explicitly.

**Tech Stack:** C/C++, SDL2, OpenGL 3.3 (via libepoxy), `linux.mk` build. No unit-test framework for the render path — verification is **build-clean + headless xvfb `-level N` boot + in-game playtest**.

## Global Constraints

- **Linux GL present path only** (`#ifndef _WIN32`, `lbUseGLPresent`). The CPU-blit fallback path must be untouched and forced to `render_scale = 100`.
- **100% must be byte-identical to current behavior** — default users see zero change. Every task preserves this.
- Setting persisted in `keeperfx.cfg` following the exact `gamma_correction` pattern in `config_settings.c`.
- v1 **excludes** CRT/scanline, integer-scale, and launcher UI (tracked as follow-ups).
- Build: `make -f linux.mk BUILD_NUMBER="$(git rev-list --count HEAD)" VER_SUFFIX=alpha -j"$(nproc)"`. Must finish with 0 errors.
- Headless boot check (per scale): `cd ~/.local/share/keeperfx-alpha && DISPLAY= SDL_AUDIODRIVER=dummy timeout -sKILL 30 xvfb-run -a -s "-screen 0 1280x720x24" ./keeperfx -level 2` then grep `keeperfx.log` for `Segmentation` (must be 0) and `Script(line` (must be ≥1).

---

### Task 1: Add the `render_scale` setting

**Files:**
- Modify: `src/config_settings.h:38-60` (struct `GameSettings`)
- Modify: `src/config_settings.c` (default ~L376, load ~L429, clamp ~L515, save ~L549)

**Interfaces:**
- Produces: `settings.render_scale` (`unsigned char`, percent) — read by Task 2. A helper `int render_scale_snap(int pct)` returning the nearest allowed value, exposed in `config_settings.h`.

- [ ] **Step 1: Add the field to the struct.** In `src/config_settings.h`, after the `gamma_correction` line, add:
```c
    unsigned short gamma_correction;
    unsigned char render_scale;   /**< Internal render resolution as a percent of native (67/75/85/100). GL present path only. */
```

- [ ] **Step 2: Add the snap helper declaration.** In `src/config_settings.h`, near the other function decls (after `extern struct GameSettings settings;`), add:
```c
int render_scale_snap(int pct); /**< Snap an arbitrary percent to the nearest allowed render_scale (67/75/85/100). */
```

- [ ] **Step 3: Implement the snap helper.** In `src/config_settings.c`, above `load_settings`, add:
```c
int render_scale_snap(int pct)
{
    static const int allowed[] = { 67, 75, 85, 100 };
    int best = 100;
    int best_d = 1000;
    for (unsigned i = 0; i < sizeof(allowed)/sizeof(allowed[0]); i++) {
        int d = abs(pct - allowed[i]);
        if (d < best_d) { best_d = d; best = allowed[i]; }
    }
    return best;
}
```

- [ ] **Step 4: Default value.** In `src/config_settings.c` near L376, after the `gamma_correction` default, add:
```c
    settings.gamma_correction              = 0;
    settings.render_scale                  = 100;
```

- [ ] **Step 5: Load from cfg.** In `src/config_settings.c` near L429, after the `gamma_correction` load line, add:
```c
        val = value_dict_get(vsec, "render_scale");
        if (val && value_type(val) == VALUE_INT32) settings.render_scale = (unsigned char)value_int32(val);
```

- [ ] **Step 6: Clamp/snap after load.** In `src/config_settings.c` near L515, after the `gamma_correction` clamp, add:
```c
    settings.render_scale = (unsigned char)render_scale_snap(settings.render_scale);
```

- [ ] **Step 7: Save to cfg.** In `src/config_settings.c` near L549, after the `gamma_correction` TOSAVE, add:
```c
    TOSAVE("render_scale = %d\n", (int)settings.render_scale);
```

- [ ] **Step 8: Build.**
Run: `make -f linux.mk BUILD_NUMBER="$(git rev-list --count HEAD)" VER_SUFFIX=alpha -j"$(nproc)"`
Expected: 0 errors.

- [ ] **Step 9: Verify round-trip.** Run the built binary once headless so it writes the cfg, then confirm the key is present:
Run: `grep render_scale ~/.local/share/keeperfx-alpha/keeperfx.cfg`
Expected: `render_scale = 100`

- [ ] **Step 10: Commit.**
```bash
git add src/config_settings.h src/config_settings.c
git commit -m "feat(video): add render_scale setting (default 100, no behavior change yet)"
```

---

### Task 2: Scale the render surface + GL framebuffer

**Files:**
- Modify: `src/bflib_video.c` `LbScreenSetup` (surface create ~L664, `gl_present_init` ~L667, PhysicalScreen ~L703-709, graphics window ~L718)

**Interfaces:**
- Consumes: `settings.render_scale` (Task 1).
- Produces: a global `void lb_render_scale_dims(int mode_w, int mode_h, int *out_w, int *out_h)` in `bflib_video.c` (declared in `bflib_video.h`) that Task 3 (mouse) also uses to know the surface size vs window size. Also sets the file-scope `int lbRenderSurfaceW, lbRenderSurfaceH;` (the scaled surface dims) and leaves the window/drawable native.

- [ ] **Step 1: Add the dims helper + globals.** In `src/bflib_video.c`, near the top (after includes), add:
```c
int lbRenderSurfaceW = 0;  /* scaled 8-bit surface width  (== window width  at 100%) */
int lbRenderSurfaceH = 0;  /* scaled 8-bit surface height (== window height at 100%) */

void lb_render_scale_dims(int mode_w, int mode_h, int *out_w, int *out_h)
{
    int pct = 100;
#ifndef _WIN32
    extern struct GameSettings settings;
    pct = (int)settings.render_scale;
    if (pct < 50 || pct > 100) pct = 100;
#endif
    /* 100% must be EXACTLY native — no rounding — to stay byte-identical. */
    if (pct >= 100) { *out_w = mode_w; *out_h = mode_h; return; }
    /* Round to even to avoid half-texel chroma seams; never below a sane floor. */
    int w = (mode_w * pct + 50) / 100; w &= ~1; if (w < 320) w = mode_w;
    int h = (mode_h * pct + 50) / 100; h &= ~1; if (h < 200) h = mode_h;
    *out_w = w; *out_h = h;
}
```
Add to `src/bflib_video.h`:
```c
extern int lbRenderSurfaceW, lbRenderSurfaceH;
void lb_render_scale_dims(int mode_w, int mode_h, int *out_w, int *out_h);
```

- [ ] **Step 2: Size the GL draw surface + framebuffer scaled.** In `src/bflib_video.c` at the GL branch (~L664), replace:
```c
        SDL_Surface* glDrawSurface = SDL_CreateRGBSurface(0, mdinfo->Width, mdinfo->Height, lbEngineBPP, 0, 0, 0, 0);
        if (glDrawSurface == NULL) {
            ERRORLOG("Can't create engine surface for mode %d (%s): %s", (int)mode, mdinfo->Desc, SDL_GetError());
        } else if (gl_present_init(lbWindow, mdinfo->Width, mdinfo->Height)) {
```
with:
```c
        int rs_w, rs_h;
        lb_render_scale_dims(mdinfo->Width, mdinfo->Height, &rs_w, &rs_h);
        SDL_Surface* glDrawSurface = SDL_CreateRGBSurface(0, rs_w, rs_h, lbEngineBPP, 0, 0, 0, 0);
        if (glDrawSurface == NULL) {
            ERRORLOG("Can't create engine surface for mode %d (%s): %s", (int)mode, mdinfo->Desc, SDL_GetError());
        } else if (gl_present_init(lbWindow, rs_w, rs_h)) {
            lbRenderSurfaceW = rs_w; lbRenderSurfaceH = rs_h;
```
(Note: the original `} else if (gl_present_init(...)) {` line's `{` now has `lbRenderSurfaceW/H` set as its first statements — keep the existing body that follows.)

- [ ] **Step 3: Make PhysicalScreen + graphics window use the surface dims, not mode dims.** In `src/bflib_video.c` ~L703, replace the four references so the engine treats the scaled surface as the screen. Change:
```c
    lbDisplay.PhysicalScreenWidth = mdinfo->Width;
    lbDisplay.PhysicalScreenHeight = mdinfo->Height;
```
to:
```c
    lbDisplay.PhysicalScreenWidth = lbDrawSurface->w;
    lbDisplay.PhysicalScreenHeight = lbDrawSurface->h;
```
and ~L709 `lbDisplay.GraphicsScreenHeight = mdinfo->Height;` to `lbDisplay.GraphicsScreenHeight = lbDrawSurface->h;`
and ~L718-719:
```c
    LbScreenSetGraphicsWindow(0, 0, mdinfo->Width, mdinfo->Height);
    LbTextSetWindow(0, 0, mdinfo->Width, mdinfo->Height);
```
to:
```c
    LbScreenSetGraphicsWindow(0, 0, lbDrawSurface->w, lbDrawSurface->h);
    LbTextSetWindow(0, 0, lbDrawSurface->w, lbDrawSurface->h);
```
> Rationale: at 100% `lbDrawSurface->w == mdinfo->Width`, so this is a no-op for default users (preserves byte-identity). At <100% it feeds the scaled dims into `setup_screen_mode`'s `width/height` → `MyScreenWidth = width*psize`, so the GUI re-lays-out proportionally with no further code.

- [ ] **Step 4: Force 100% on the non-GL fallback.** The CPU-blit branch (`if (!lbUseGLPresent)`, ~L679) already creates surfaces at `mdinfo->Width/Height`; leave it. Add right after that branch sets up its surface, `lbRenderSurfaceW = lbDrawSurface->w; lbRenderSurfaceH = lbDrawSurface->h;` so the mouse transform (Task 3) sees a 1:1 surface on the fallback path.

- [ ] **Step 5: Build.**
Run: `make -f linux.mk BUILD_NUMBER="$(git rev-list --count HEAD)" VER_SUFFIX=alpha -j"$(nproc)"`
Expected: 0 errors.

- [ ] **Step 6: Headless boot at 100% (identity guard) and at 75%.** Set `render_scale = 100` in the cfg, run the boot check → 0 segfaults, reaches level. Then set `render_scale = 75`, run again → 0 segfaults, reaches level. (Image will be NEAREST-blocky at 75% until Task 4; that's expected.)
Run (both): the Global-Constraints headless boot check.
Expected: `Segmentation` count 0; `Script(line` count ≥1; log shows `Mode <scaled>x<scaled>x8 setup succeeded` at 75%.

- [ ] **Step 7: Commit.**
```bash
git add src/bflib_video.c src/bflib_video.h
git commit -m "feat(video): render the 8-bit surface at render_scale%, upscale via GL present"
```

---

### Task 3: Map the mouse from window space to scaled-surface space

**Files:**
- Modify: `src/bflib_mouse.cpp` (`LbMoveGameCursorToHostCursor` ~L135, and the pointer-set path used for input)
- Reference: `src/bflib_video.c` (`lbRenderSurfaceW/H`, window drawable size)

**Interfaces:**
- Consumes: `lbRenderSurfaceW/H` (Task 2), the window drawable size (`SDL_GL_GetDrawableSize(lbWindow, ...)`).
- Produces: correct cursor tracking + click-picking at every scale.

**Context:** SDL reports the host cursor in **window/drawable pixels** (e.g. 0..3440). The game cursor lives in **surface pixels** (e.g. 0..2580 at 75%). Because the GL present upscales the surface to fill the letterboxed viewport (aspect matches → full window), the mapping is a uniform scale `surface_dim / window_dim` per axis (no offset when aspect matches; include the letterbox offset for safety). At 100% this is identity.

- [ ] **Step 1: Add a window→surface mapping helper.** In `src/bflib_mouse.cpp`, near the top, add:
```c
static void host_to_surface(int host_x, int host_y, int *sx, int *sy)
{
    int win_w = 0, win_h = 0;
    SDL_GL_GetDrawableSize(lbWindow, &win_w, &win_h);
    int surf_w = lbRenderSurfaceW > 0 ? lbRenderSurfaceW : win_w;
    int surf_h = lbRenderSurfaceH > 0 ? lbRenderSurfaceH : win_h;
    if (win_w <= 0 || win_h <= 0) { *sx = host_x; *sy = host_y; return; }
    /* Letterbox the surface into the window the same way gl_present_frame does. */
    int vp_w = win_w, vp_h = (int)((long long)win_w * surf_h / surf_w);
    if (vp_h > win_h) { vp_h = win_h; vp_w = (int)((long long)win_h * surf_w / surf_h); }
    int vp_x = (win_w - vp_w) / 2, vp_y = (win_h - vp_h) / 2;
    int rx = host_x - vp_x, ry = host_y - vp_y;
    if (rx < 0) rx = 0; if (rx >= vp_w) rx = vp_w - 1;
    if (ry < 0) ry = 0; if (ry >= vp_h) ry = vp_h - 1;
    *sx = (int)((long long)rx * surf_w / vp_w);
    *sy = (int)((long long)ry * surf_h / vp_h);
}
```
Ensure `bflib_video.h` (for the externs) and `<SDL2/SDL.h>` are included in this file (they are — it already uses `SDL_GetMouseState`/`lbWindow`).

- [ ] **Step 2: Apply it on host→game input.** In `LbMoveGameCursorToHostCursor` (~L135), change:
```c
    SDL_GetMouseState(&host_cursor_x, &host_cursor_y);
    if (((host_cursor_x != game_cursor_x) || (host_cursor_y != game_cursor_y)) && LbIsActive())
    {
        if (!pointerHandler.SetMousePosition(host_cursor_x, host_cursor_y))
```
to:
```c
    SDL_GetMouseState(&host_cursor_x, &host_cursor_y);
    int surf_x, surf_y;
    host_to_surface(host_cursor_x, host_cursor_y, &surf_x, &surf_y);
    if (((surf_x != game_cursor_x) || (surf_y != game_cursor_y)) && LbIsActive())
    {
        if (!pointerHandler.SetMousePosition(surf_x, surf_y))
```

- [ ] **Step 3: Fix the reverse (game→host warp) so it maps surface→window.** In `LbMoveHostCursorToGameCursor` (~L124) the `SDL_GetMouseState` compare is only used to detect drift; the actual warp is `LbMouseSetPosition(game_cursor_x, game_cursor_y)` which warps in **window** coords (`SDL_WarpMouseInWindow`, ~L119). Change `LbMouseSetPosition`'s warp target to surface→window. In the function that calls `SDL_WarpMouseInWindow(window, x, y)` (~L119), map `x,y` (surface) up to window: add before the warp:
```c
  int win_w = 0, win_h = 0;
  SDL_GL_GetDrawableSize(window, &win_w, &win_h);
  int surf_w = lbRenderSurfaceW > 0 ? lbRenderSurfaceW : win_w;
  int surf_h = lbRenderSurfaceH > 0 ? lbRenderSurfaceH : win_h;
  if (surf_w > 0 && surf_h > 0) {
      int vp_w = win_w, vp_h = (int)((long long)win_w * surf_h / surf_w);
      if (vp_h > win_h) { vp_h = win_h; vp_w = (int)((long long)win_h * surf_w / surf_h); }
      x = (win_w - vp_w) / 2 + (int)((long long)x * vp_w / surf_w);
      y = (win_h - vp_h) / 2 + (int)((long long)y * vp_h / surf_h);
  }
  SDL_WarpMouseInWindow(window, x, y);
```

- [ ] **Step 4: Build.**
Run: `make -f linux.mk BUILD_NUMBER="$(git rev-list --count HEAD)" VER_SUFFIX=alpha -j"$(nproc)"`
Expected: 0 errors.

- [ ] **Step 5: Headless boot at 75% (regression guard only — mouse accuracy is a playtest).**
Run: the Global-Constraints headless boot check with `render_scale = 75`.
Expected: `Segmentation` 0, reaches level.

- [ ] **Step 6: USER PLAYTEST GATE.** Deploy to `~/.local/share/keeperfx-alpha` and ask the user to verify, at 75% and 100%: the cursor sits under the real pointer across the whole screen (corners especially), and clicks pick the correct creature/room/panel button. **Do not proceed to Task 4 until confirmed** — this is the primary risk.

- [ ] **Step 7: Commit.**
```bash
git add src/bflib_mouse.cpp
git commit -m "feat(video): map mouse window<->scaled-surface coords for render_scale"
```

---

### Task 4: In-shader sharp-bilinear upscale

**Files:**
- Modify: `src/bflib_render_gl.c` (scene-pass `fragment_src` ~L196-207, uniform locations ~L845, `gl_run_scene_pass` ~L961)

**Interfaces:**
- Consumes: `gl.fb_width/fb_height` (the framebuffer size, already tracked in the struct).
- Produces: clean upscaled output at <100%; **byte-identical NEAREST at 1:1**.

**Context:** The index texture is `GL_R8UI` (`GL_NEAREST` mandatory — you cannot linearly filter palette indices). Sharp-bilinear resolves by sampling the 4 neighbor index texels with `texelFetch`, palette-looking-up each to RGB, then blending with bilinear weights whose fractional coordinate is passed through a "sharpen" ramp so pixel edges stay crisp.

- [ ] **Step 1: Replace the scene-pass fragment shader.** In `src/bflib_render_gl.c`, replace `fragment_src` (~L196-207) with:
```c
static const char *fragment_src =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "out vec4 outColor;\n"
    "uniform usampler2D u_indexed;\n"
    "uniform sampler2D u_palette;\n"
    "uniform vec2 u_fb_size;\n"   /* framebuffer size in texels */
    "vec4 pal(ivec2 p){ uint idx = texelFetch(u_indexed, p, 0).r;"
    "                   return texelFetch(u_palette, ivec2(int(idx),0), 0); }\n"
    "void main(void)\n"
    "{\n"
    "    vec2 t = v_uv * u_fb_size - 0.5;\n"
    "    vec2 base = floor(t);\n"
    "    vec2 f = t - base;\n"
    "    /* sharpen the interpolation: near-nearest, antialiased only across the\n"
    "       one-texel edge (fwidth-based ramp keeps it crisp and shimmer-free) */\n"
    "    vec2 fw = fwidth(t);\n"
    "    f = clamp((f - 0.5) / max(fw, vec2(1e-4)) + 0.5, 0.0, 1.0);\n"
    "    ivec2 b = ivec2(base);\n"
    "    ivec2 mx = ivec2(u_fb_size) - 1;\n"
    "    ivec2 b0 = clamp(b,               ivec2(0), mx);\n"
    "    ivec2 b1 = clamp(b + ivec2(1,0),  ivec2(0), mx);\n"
    "    ivec2 b2 = clamp(b + ivec2(0,1),  ivec2(0), mx);\n"
    "    ivec2 b3 = clamp(b + ivec2(1,1),  ivec2(0), mx);\n"
    "    vec4 c01 = mix(pal(b0), pal(b1), f.x);\n"
    "    vec4 c23 = mix(pal(b2), pal(b3), f.x);\n"
    "    outColor = mix(c01, c23, f.y);\n"
    "}\n";
```
> At exact 1:1 (viewport == fb), each fragment centers on a texel: `f` snaps to 0 or 1 via the ramp, so the mix selects a single texel — identical to the old NEAREST path.

- [ ] **Step 2: Fetch the new uniform location.** In `src/bflib_render_gl.c` near where `gl.loc_indexed`/`gl.loc_palette` are queried (~L845), add a `gl.loc_fb_size` field to the `struct` holding these (search `loc_palette;` in the struct near L100 and add `GLint loc_fb_size;`), then after the existing `glGetUniformLocation` calls add:
```c
    gl.loc_fb_size = glGetUniformLocation(gl.program, "u_fb_size");
```

- [ ] **Step 3: Upload the uniform in the scene pass.** In `gl_run_scene_pass` (~L961), after `glUniform1i(gl.loc_palette, 1);`, add:
```c
    glUniform2f(gl.loc_fb_size, (float)gl.fb_width, (float)gl.fb_height);
```

- [ ] **Step 4: Build.**
Run: `make -f linux.mk BUILD_NUMBER="$(git rev-list --count HEAD)" VER_SUFFIX=alpha -j"$(nproc)"`
Expected: 0 errors (watch for GLSL compile errors logged at runtime, not build time).

- [ ] **Step 5: Headless boot at 100% and 75%, check for GL/shader errors.**
Run: boot check at each; additionally grep the log: `grep -iE "shader|glsl|gl_check_error|compile" keeperfx.log`
Expected: no shader-compile errors; `Segmentation` 0; reaches level at both scales.

- [ ] **Step 6: USER PLAYTEST GATE.** Deploy and have the user compare 100% (must look exactly as before) vs 75%/67% (should look clean — sharp but not blocky, no shimmer on camera pan). Confirm the image quality is acceptable and 100% is unchanged.

- [ ] **Step 7: Commit.**
```bash
git add src/bflib_render_gl.c
git commit -m "feat(video): in-shader sharp-bilinear upscale for render_scale"
```

---

### Task 5: Performance check, docs, and finalize

**Files:**
- Modify: `README.md` ("Linux performance" section)

- [ ] **Step 1: Rough performance measurement.** With a deterministic scenario (`-level 2`, fixed wall-time headless run), compare CPU frame-time / turns completed at `render_scale = 100` vs `67`. Record the delta (expect a meaningful CPU reduction at 67%). Note: headless xvfb has no vsync so it reflects render throughput.

- [ ] **Step 2: Document in README.** In `README.md` under "Linux performance", add a bullet:
```markdown
- **Adjustable render scale.** Render the game at 67–100% of native resolution (`render_scale` in
  `keeperfx.cfg`) for a CPU-side performance gain on the software rasterizer, upscaled to your window
  with a sharp-bilinear filter (crisp, not blurry). 100% (default) is unchanged.
```

- [ ] **Step 3: Commit.**
```bash
git add README.md
git commit -m "docs(readme): document adjustable render scale"
```

- [ ] **Step 4: Final full verification.** Clean rebuild; headless boot at 67/75/85/100 (all 0 segfaults, reach level); confirm `render_scale = 100` diff-clean vs `alpha` in observable behavior. Hand to user for a full playtest (a complete level at 75%) before merge.

---

## Self-Review notes
- **Spec coverage:** setting (T1), surface sizing + GUI auto-scale (T2), mouse mapping (T3), sharp-bilinear filter (T4), perf check + docs (T5), 100%-identity preserved in T2/T4, non-GL forced 100 in T2. All spec sections mapped.
- **Byte-identity at 100%:** T2 dims collapse to native (`w&~1` at 100% of an even native res == native; if native is odd, the `&~1` would shift by 1 — **flagged**: at 100% skip the `&~1` and use mode dims exactly. Add to T2 Step 1: `if (pct >= 100) { *out_w = mode_w; *out_h = mode_h; return; }`). T4 shader collapses to NEAREST at 1:1. T3 transform is identity when surf==win.
- **Risk isolation:** mouse (T3) and shader (T4) each have their own playtest gate; nothing after depends on an unverified prior task.
