# Truecolor GPU World Renderer Implementation Plan (Sub-project 1)

**Goal:** Render the KeeperFX 3D world view on the GPU in 32-bit truecolor, reproducing the current software look exactly, so hi-res truecolor assets (Sub-project 2) become a drop-in texture swap.

**Architecture:** Keep the engine's CPU geometry (projection, cull, depth-sort into buckets). Intercept the **bucket-draw dispatch loop** in `engine_render.c` — instead of calling the software rasterizer (`draw_gpoly`, `draw_jonty_mapwho`, …) per sorted bucket item, submit each primitive to a new GPU world module that renders it (textured + gouraud-shaded) into a truecolor scene FBO. Composite the existing 8-bit GUI/overlays over the GPU world (reusing the post-FX present), then run the existing post-FX.

**Tech Stack:** C (C11) / OpenGL 3.3 core via libepoxy, SDL2, the existing `bflib_render_gl` GPU present + post-FX pipeline. Build: CMake + Ninja, `cmake --build bin-linux --target keeperfx -- -k 0`.

## Global Constraints

- Target = Linux x86-64 (LP64). All GL/world code fenced `#ifndef _WIN32`; the Windows build keeps the software rasterizer. (verbatim from spec §8)
- `-Werror` clean (it is ON for the Linux target). (spec §8)
- **Success = visual parity:** with palette-converted existing textures, the GPU world render must be visually indistinguishable from the software render. No visible improvement is expected in this sub-project. (spec §1)
- Watch LP64 hazards in new math: `long` width (8 bytes), the `min(a-b,b-a)` signed-promotion abs trick. Use fixed-width types for anything packed. (spec §8)
- Do NOT modify `/opt`, vendored deps, or the engine's geometry/sim. Edits confined to: new `src/src/bflib_render_glworld.{c,h}`, the dispatch-loop branches in `src/src/engine_render.c`, the composite extension in `src/src/bflib_render_gl.c`, and lifecycle wiring in `src/src/bflib_video.c`. (spec §8)
- Fallback: `KFX_GLWORLD=0` or any init/FBO/shader failure → `gl_world_active=false` → software rasterizer runs (game always works). (spec §7)
- Keep the GL backend interface clean so a Vulkan backend could later implement the same `glworld_*` surface. (spec §8)

## Verification model (no unit-test framework — this is a renderer)

Each task's "test" is **build + runtime + visual parity**. The parity check, used throughout:
```bash
# Build, then capture the SAME scene with GPU world ON vs OFF and diff.
export DISPLAY=:1 WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
RUN=$HOME/.local/share/keeperfx-linux/run
# software reference:
KFX_GLWORLD=0 KFX_POSTFX=0 timeout 18 ./run-linux.sh -nointro -level 1 >/tmp/sw.log 2>&1 || true
cp "$RUN"/scrshots/*.png /tmp/ref.png   # (or grab via gl readback debug dump, see Task 1 step)
# GPU world:
KFX_GLWORLD=1 KFX_POSTFX=0 timeout 18 ./run-linux.sh -nointro -level 1 >/tmp/gl.log 2>&1 || true
```
Because driving the in-game camera to an identical frame is hard, Task 1 adds a **debug framebuffer-readback dump** (`KFX_GLWORLD_DUMP=1` writes the world scene texture + the software framebuffer to PNG on a fixed game-turn), so parity is compared on a deterministic frame. Human side-by-side screenshots are the final gate (per spec: the user reviews after Sub-project 2; for Sub-project 1 the implementer/controller compares dumps).

---

## File Structure

- Create: `src/src/bflib_render_glworld.h` — the GPU world backend interface.
- Create: `src/src/bflib_render_glworld.c` — implementation (GL resources, texture store, batches, shaders, flush).
- Modify: `src/src/engine_render.c` — branch the bucket-draw dispatch to GPU submit when `gl_world_active`.
- Modify: `src/src/bflib_render_gl.c` — composite the GPU world scene under the 8-bit GUI/overlay palette pass.
- Modify: `src/src/bflib_video.c` — `glworld_init/shutdown` lifecycle + `gl_world_active` flag + `KFX_GLWORLD`.

---

## Task 1: GL world module scaffolding, scene FBO, lifecycle, debug dump

**Files:**
- Create: `src/src/bflib_render_glworld.h`, `src/src/bflib_render_glworld.c`
- Modify: `src/src/bflib_video.c` (lifecycle + flag), `src/src/CMakeLists.txt` (glob picks up the new .c automatically — verify)

**Interfaces:**
- Produces: `extern TbBool gl_world_active;`
  `TbBool glworld_init(int world_w, int world_h);` (true on success)
  `void glworld_begin_frame(int win_x, int win_y, int win_w, int win_h);`
  `void glworld_end_frame(void);`
  `GLuint glworld_scene_texture(void);`
  `void glworld_shutdown(void);`
  `void glworld_debug_dump(const char *path);` (readback the scene FBO to PNG)
- Consumes: the existing GL context created by `bflib_render_gl` (Task ordering: GL present init must run first; `glworld_init` reuses the current context).

- [ ] **Step 1: Write the header `bflib_render_glworld.h`**

```c
#pragma once
#ifndef _WIN32
#include <epoxy/gl.h>
#endif
#include "bflib_basics.h"
#ifdef __cplusplus
extern "C" {
#endif
extern TbBool gl_world_active; // set by bflib_video on successful init + KFX_GLWORLD
#ifndef _WIN32
TbBool glworld_init(int world_w, int world_h);
void   glworld_begin_frame(int win_x, int win_y, int win_w, int win_h);
void   glworld_end_frame(void);
GLuint glworld_scene_texture(void);
void   glworld_shutdown(void);
void   glworld_debug_dump(const char *path);
#endif
#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: Implement init/shutdown + RGBA16F scene FBO in `bflib_render_glworld.c`**

Create a `GL_RGBA16F` scene texture sized `world_w × world_h` + an FBO, plus a depth-or-not (no depth in this sub-project; painter's order). Check `glCheckFramebufferStatus`; log + return false on failure. `glworld_scene_texture()` returns the scene texture. `glworld_begin_frame` binds the FBO, `glViewport(win_x, win_y, win_w, win_h)` (note GL bottom-left origin — convert from the engine window's top-left rect), `glClear` to transparent black. `glworld_end_frame` unbinds. Model the structure on the existing `bflib_render_gl.c` (FBO/texture creation, `gl_check_error`, `LbSyncLog`). `gl_world_active` defaults false.

- [ ] **Step 3: Implement `glworld_debug_dump`** — `glReadPixels` the scene FBO into a buffer, write a PNG (reuse spng which is already linked, or a raw PPM if simpler). This is the parity tool.

- [ ] **Step 4: Wire lifecycle in `bflib_video.c`** — after `gl_present_init` succeeds in `setup_screen_mode`, call `glworld_init(world_w, world_h)`; set `gl_world_active = (KFX_GLWORLD != "0") && glworld_init_ok`. Add `glworld_shutdown()` to the teardown beside `gl_present_shutdown()`. Read `KFX_GLWORLD` env (default on). Guard all calls `#ifndef _WIN32`.

- [ ] **Step 5: Build + verify no behavior change**
```bash
cmake --build bin-linux --target keeperfx -- -k 0 2>&1 | grep -E 'error:|warning:' | head
```
Expected: empty, exit 0. Run the game (`run-linux.sh -level 1`): it still renders via software (nothing submits to the world yet); log shows "glworld: init ok (WxH RGBA16F)". No crash.

- [ ] **Step 6: Commit**
```bash
git add src/src/bflib_render_glworld.h src/src/bflib_render_glworld.c src/src/bflib_video.c
git commit -m "feat(glworld): scene FBO, lifecycle, debug dump scaffolding"
```

---

## Task 2: Textured + gouraud-shaded triangle pipeline + terrain interception

**Files:**
- Modify: `src/src/bflib_render_glworld.{c,h}` (triangle batch, shaders, texture store, submit/flush)
- Modify: `src/src/engine_render.c` (intercept the polygon dispatch)

**Interfaces:**
- Produces:
  `struct GlWorldVert { float sx, sy, u, v, shade; };`
  `void glworld_submit_tri(const struct GlWorldVert v[3], uint16_t texture_id);`
  `void glworld_texstore_sync(void);` (rebuild/refresh the GPU texture store from `block_ptrs`)
- Consumes: from `engine_render.c` — the per-bucket `PolyPoint{X,Y,U,V,S}` (screen-space `X,Y`; UV in 0..0x1FFFFF fixed-point over the 32px tile; shade `S`) and the bucket's `block` texture id; the active 256-color palette (`lbPaletteColors`).

- [ ] **Step 1: GPU texture store from the paletted tiles**
The terrain tiles live in `block_ptrs[i]` → 32×32 bytes of palette indices. Build a `GL_RGBA8` 2D **texture array** (layer per tile id, 32×32) by converting each tile's indices through the current palette. `glworld_texstore_sync()` (re)builds it; call it on init and when the palette or `block_ptrs` change (animation). Use `GL_NEAREST` (match the software point-sampled look; hi-res in SP2 can switch to linear). Cite: `block_mem`/`block_ptrs` in `engine_textures.c:36-37`.

- [ ] **Step 2: Triangle shader program**
Vertex: take `(sx, sy)` in engine-window pixels → NDC using the window size uniform; pass `(u,v)` and `shade`. Fragment: `vec4 t = texture(u_tiles, vec3(uv, layer)); outColor = vec4(t.rgb * shade, t.a);` where `shade` is the per-vertex brightness derived from `PolyPoint.S`. Determine the exact `S → shade` mapping by matching the software rasterizer's shade handling (`bflib_render_gpoly.c` uses `S >> 16` as the shade index into `render_fade_tables`); replicate so brightness matches. **Parity-critical:** verify against the software output (Step 6), adjust the mapping until it matches.

- [ ] **Step 3: Triangle batch + flush**
`glworld_submit_tri` appends 3 vertices (converting `PolyPoint.U/V` fixed-point 0..0x1FFFFF → 0..1, `S` → shade float) + the texture layer (`texture_id`) to a CPU batch. `glworld_end_frame` uploads the batch to a VBO and draws (batched by texture layer, or pass layer per-vertex as an attribute to draw in one call). Preserve submission order (painter's) — draw in the order submitted.

- [ ] **Step 4: Intercept the polygon dispatch in `engine_render.c`**
Find the bucket-draw dispatch where `draw_gpoly(&poly->vertex_first, &poly->vertex_second, &poly->vertex_third)` is called for `QK_PolygonStandard`/`Simple`/`NearFP` (e.g. `engine_render.c:5775`, and the main draw loop ~6680-7030). Branch: `if (gl_world_active) glworld_submit_tri(<from the 3 PolyPoints>, poly->block); else draw_gpoly(...);`. Do this for ALL polygon kinds that currently call `draw_gpoly`/`trig` with a texture. Read the dispatch loop fully and replicate each textured-polygon kind. (Bounded discovery: enumerate the kinds in the loop; each is a one-line branch.)

- [ ] **Step 5: Drive begin/end around the world draw**
In the world-view render (where the bucket loop runs, inside `redraw_isometric_view`/`draw_view`), wrap the bucket-draw with `glworld_begin_frame(win)`/`glworld_end_frame()` when `gl_world_active`. Get the engine window rect from `player->engine_window_x/y/width/height` (or the `ewnd` used by `setup_vecs`).

- [ ] **Step 6: Build + PARITY verify (terrain only)**
Build clean. Capture a deterministic frame both ways using the Task 1 debug dump (`KFX_GLWORLD_DUMP=1` on a fixed game turn) with `KFX_POSTFX=0`. Compare the terrain (floors/walls) regions:
```bash
magick compare -metric AE /tmp/ref.png /tmp/glworld.png /tmp/diff.png ; echo
```
Expected: terrain matches closely (sprites still software at this point — ignore sprite regions). Iterate the shade/UV mapping (Step 2) until terrain colors/shading match. Document residual diffs.

- [ ] **Step 7: Commit**
```bash
git add src/src/bflib_render_glworld.c src/src/bflib_render_glworld.h src/src/engine_render.c
git commit -m "feat(glworld): textured gouraud terrain on GPU (parity with software)"
```

---

## Task 3: Composite GPU world under the 8-bit GUI/overlays + post-FX

**Files:**
- Modify: `src/src/bflib_render_gl.c` (present/composite path)

**Interfaces:**
- Consumes: `glworld_scene_texture()` (truecolor world), the 8-bit `lbDrawSurface` (GUI/overlays), the existing post-FX scene FBO + palette-LUT pass.
- Produces: a composited truecolor frame fed into the existing post-FX → screen.

- [ ] **Step 1: Define the world-region transparency rule**
When `gl_world_active`, the engine no longer draws the world into `lbDrawSurface` (the world region there is left at the clear/background index). Choose a transparent sentinel: the engine-window rect is known; in the composite, inside that rect, palette index `0` (or a dedicated unused index) = "show world". Confirm index 0 isn't used for visible GUI pixels in the world rect (if it is, clear the world rect to a reserved sentinel before the GUI/overlays draw — add that clear in `engine_render.c` under `gl_world_active`).

- [ ] **Step 2: Composite in the present path**
In `LbScreenSwap`'s GL branch / `gl_present_frame`: (1) blit/draw `glworld_scene_texture()` into the post-FX scene FBO at the engine-window rect; (2) run the palette-LUT pass over `lbDrawSurface` with an alpha test — inside the world rect, `discard` the sentinel index so the world shows through; elsewhere draw the GUI normally; (3) run the existing post-FX over the composited scene. Overlays (tooltips, menus, hand, text) drawn into `lbDrawSurface` over the world are non-sentinel → composite on top. Reuse the existing scene-FBO + post-FX from Task A.2.

- [ ] **Step 3: Build + verify composition**
Build clean. Run a level with `gl_world_active`. Verify: the world shows (GPU truecolor terrain), the GUI panel + HUD render correctly beside it, and overlays over the world (tooltip box, the "hand", `Training Room`-style text, the options menu) appear ON TOP of the world, not behind or clipped. Capture screenshots for the controller to eyeball.

- [ ] **Step 4: Commit**
```bash
git add src/src/bflib_render_gl.c src/src/engine_render.c
git commit -m "feat(glworld): composite GPU world under 8-bit GUI + post-FX"
```

---

## Task 4: Billboards (creatures/objects/effects), floating text, selection lines

**Files:**
- Modify: `src/src/bflib_render_glworld.{c,h}` (sprite + line pipeline, sprite texture store)
- Modify: `src/src/engine_render.c` (intercept sprite/number/line dispatch)

**Interfaces:**
- Produces:
  `void glworld_submit_sprite(float sx, float sy, float scale, uint16_t sprite_id, uint8_t angle, float shade, uint32_t flags);`
  `void glworld_submit_line(float x0, float y0, float x1, float y1, uint32_t rgba);`
- Consumes: the bucket sprite items (`BucketKindJontySprite` → `draw_jonty_mapwho`, `engine_render.c:492`/`4870`), floating gold text (`draw_engine_number`, `5041`), and `QK_SlabSelector` lines (`2378`/`2411`). `KeeperSprite` frames + the player-color remap.

- [ ] **Step 1: Sprite texture store** — upload `KeeperSprite` frames (palette→RGBA, with the transparent index as alpha=0) into a sprite texture array keyed by sprite/frame id. Handle the player-color remap (the `_remp` path) as a recolor when building the RGBA (or a remap uniform). `GL_NEAREST`.

- [ ] **Step 2: Sprite + line draw** — `glworld_submit_sprite` emits a textured quad at `(sx,sy)` sized by `scale`, sampling the sprite layer, multiplied by `shade`, alpha-blended, drawn in submission order (painter's — preserves occlusion vs terrain). `glworld_submit_line` emits a colored line. Add to `glworld_end_frame`'s flush (interleaved in submission order with triangles — keep one ordered command list so depth order across kinds is preserved).

- [ ] **Step 3: Intercept sprite/number/line dispatch** in `engine_render.c` — branch `draw_jonty_mapwho`, `draw_engine_number`, and the `QK_SlabSelector` line draw to the `glworld_submit_*` equivalents when `gl_world_active`. Read each from the dispatch loop and replicate its screen pos/scale/shade.

- [ ] **Step 4: Build + parity verify (full world)** — build clean; capture deterministic dumps GPU-vs-software (`KFX_POSTFX=0`); verify the FULL world (terrain + creatures + objects + effects + selection box) matches, including **occlusion** (a creature behind a wall stays occluded — confirm submission/painter's order is preserved). Iterate until parity.

- [ ] **Step 5: Commit**
```bash
git add src/src/bflib_render_glworld.c src/src/bflib_render_glworld.h src/src/engine_render.c
git commit -m "feat(glworld): GPU billboards, floating text, selection lines (full-world parity)"
```

---

## Task 5: Parity hardening, performance, and full-level soak

**Files:** Modify any of the above for fixes found.

- [ ] **Step 1: Side-by-side parity sweep** — capture several scenes (heart room, corridors, combat, possession view, front-view mode if reachable) GPU-vs-software with `KFX_POSTFX=0`; diff. Fix any shading/UV/order discrepancies. Record any intentionally-accepted differences.

- [ ] **Step 2: Animated tiles + palette changes** — verify animated terrain (lava, the texture-anim tiles) and palette effects (the freeze/possession palettes) still update correctly through `glworld_texstore_sync()` (the per-frame palette sync added in the present path covers display palette; ensure the tile store rebuilds on `block_ptrs`/palette change).

- [ ] **Step 3: Performance** — confirm full frame rate at 3440×1440 with `gl_world_active` (RTX-class GPU; expect trivial). Log frame timing if needed. Confirm no per-frame full-texture-store rebuild unless changed.

- [ ] **Step 4: Soak + fallback** — play through a full level with `gl_world_active`: no crashes, no GL errors. Then `KFX_GLWORLD=0`: confirm the software path still works (fallback intact). Force an init failure (e.g. temporarily break the shader) and confirm graceful fallback to software, game still runs.

- [ ] **Step 5: -Werror clean + both targets**
```bash
cmake --build bin-linux -- -k 0 2>&1 | grep -E 'error:|warning:' | head; echo "exit: $?"
```
Expected: empty, both `keeperfx` + `keeperfx_hvlog` build, exit 0.

- [ ] **Step 6: Commit + update the spec status**
```bash
git add -A
git commit -m "feat(glworld): parity/perf hardening; Sub-project 1 complete (truecolor GPU world, parity)"
```

---

## Notes for the implementer

- **Parity is the whole game here.** Sub-project 1 should be invisible to the player. If it looks different, it's a bug — match the software path. The shade (`PolyPoint.S` → brightness) and UV (0..0x1FFFFF fixed → 0..1) mappings are the most likely parity pitfalls; verify with the debug dumps.
- **Submission order = painter's order.** Don't add a depth buffer in this sub-project; preserve the bucket's sorted submission order across triangles AND sprites in one ordered command list, or occlusion will break.
- **The texture store is the seam to Sub-project 2.** Keep tile/sprite upload behind `glworld_texstore_sync()` so SP2 can fill the same slots from a hi-res pack without touching the renderer.
- **Don't touch the engine geometry/sim** — only the dispatch branches and the new module.
- The exact dispatch-loop line ranges and the full set of bucket kinds are read during implementation (the loop is the single interception site); each kind is a one-line branch to a `glworld_submit_*`.
