# Truecolor GPU World Renderer — Design Spec (Sub-project 1)

**Status:** Sub-project 1 complete — truecolor GPU world renderer (parity-first) implemented and hardened (Tasks 1–5). Opt-in via `KFX_GLWORLD=1`; software path unchanged. Live full-level visual sign-off deferred to after Sub-project 2.
**Branch:** `linux-native-port`
**Date:** 2026-06-25

## 1. Goal

Render the KeeperFX 3D world view on the GPU in **32-bit truecolor**, replacing the 8-bit software rasterizer for the world, while **reproducing the current look exactly** as the initial deliverable. This is the foundation that later (Sub-project 2) lets us swap the engine's 32×32 paletted terrain tiles and paletted sprites for **hi-res truecolor assets**, which is where the visible "modernization" lands.

This spec covers **only the renderer foundation**. Hi-res asset generation/loading is Sub-project 2; per-pixel/normal-mapped lighting and a Vulkan backend are later phases.

### Success = visual parity
The defining success criterion for Sub-project 1: with the existing (palette-converted) textures, the GPU world render is **visually indistinguishable** from the current software render (same geometry, same colors, same shading, same sprite placement/occlusion), running at full speed, with no crashes through a full level. No "improvement" is expected yet — the win is the architecture that unlocks it.

## 2. Background (engine facts this design relies on)

- The engine builds a **depth-sorted bucket** of world primitives per frame (`get_bucket_item`, `BasicQ`/`QKinds` in `engine_render.c`): textured floor/wall triangles (`QK_PolygonStandard` → `do_a_gpoly_gourad_tr/bl`), creature/object **billboards** (`draw_jonty_mapwho` → `KeeperSprite`), effects, floating numbers, and selection-box lines (`QK_SlabSelector`). All projection, culling, and depth-sorting happen on the CPU; the rasterizer just fills pixels.
- **Terrain textures** are 32×32 **8-bit paletted** tiles in `block_mem` / `block_ptrs[]` (`engine_textures.c`), animated via `block_ptrs` swaps.
- **Sprites** are `KeeperSprite` paletted frames (8-directional, per-animation).
- The world view renders into a sub-rectangle (the "engine window") of the 8-bit framebuffer `lbDrawSurface`; the **GUI panel/HUD/menus/tooltips/hand/text** are drawn separately into the same 8-bit framebuffer (not in the world bucket).
- A working **GPU present backend** already exists (`bflib_render_gl.{c,h}`, OpenGL 3.3 core via libepoxy): palette-LUT present + a post-FX pipeline (bloom/tonemap/grade), with a CPU-blit fallback. We extend this, we do not replace it.

## 3. Architecture & data flow

Keep all the engine's CPU work (projection, culling, depth-sort). Redirect only the **pixel filling** of the world from the software rasterizer to the GPU, in truecolor.

```
engine builds depth-sorted world bucket  (unchanged)
        │
        ▼  per-primitive draw fn:  if (gl_world_active) submit to GPU batch; else software-fill
   GPU batches (verts: screen x/y, UV, per-vertex shade, texture id; sprites: pos/scale/spriteid/shade; lines)
        │  glworld_end_frame(): flush batches in bucket order
        ▼
   truecolor World/scene FBO (RGBA16F) ──┐
                                          │  composite (existing gl present):
   8-bit GUI/HUD/overlays (lbDrawSurface) ┤   palette-LUT pass draws GUI over the world,
                                          │   transparent where the world should show through
                                          ▼
                            existing post-FX (bloom/grade) → screen
```

### 3.1 Interception
Each per-primitive draw function in `engine_render.c` gets a thin branch:
- `do_a_gpoly_gourad_tr` / `do_a_gpoly_gourad_bl` (lit terrain/walls) and the unlit variants → `glworld_submit_triangle(...)`.
- `draw_jonty_mapwho` and the keepersprite billboard path → `glworld_submit_sprite(...)`.
- effect/number/line primitives (`QK_SlabSelector`, floating gold text) → corresponding submit calls.
- When `gl_world_active == false`, the existing software fill runs unchanged.

Submission order = bucket order, preserving the engine's painter's-algorithm sort (a real depth buffer is a later refinement, **not** in scope here — we match current behavior).

### 3.2 World/GUI composition
The world (everything in the 3D bucket — terrain, sprites, effects, selection box) renders to a truecolor scene FBO. The GUI/HUD/menus/overlays remain 8-bit in `lbDrawSurface`. The existing GL present's palette-LUT pass composites the 8-bit layer **over** the GPU world: where the world view region was not drawn by the GUI (a chosen transparent sentinel / the engine-window rect), the truecolor world shows through; panels, tooltips, the "hand", and text paint on top. The existing post-FX then runs on the composited frame, so all current bloom/grade work carries over automatically.

The GUI stays 8-bit in this sub-project (it is the side panel, not the brown-terrain culprit). Upgrading the GUI to hi-res is out of scope here.

## 4. Components & interfaces

### 4.1 New module `bflib_render_glworld.{c,h}`
GPU world backend, fenced `#ifndef _WIN32`, libepoxy/GL 3.3. Public interface (C, `extern "C"`-safe):
- `TbBool glworld_init(int world_w, int world_h);` — create the truecolor scene FBO, GPU vertex/index buffers, the textured-triangle and sprite shader programs, and the **texture store** (see §5). Returns false on any failure (caller falls back to software render).
- `void glworld_begin_frame(int win_x, int win_y, int win_w, int win_h);` — start a frame: bind the scene FBO, set the engine-window viewport, reset batches.
- `void glworld_submit_triangle(const struct GlWorldVert v[3], uint16_t texture_id);` — append a textured/shaded triangle. `GlWorldVert = { float sx, sy; float u, v; float shade; }` (screen-space position, tile UV, per-vertex shade 0..1).
- `void glworld_submit_sprite(float sx, float sy, float scale, uint16_t sprite_id, uint8_t angle, float shade, uint32_t flags);` — append a billboard.
- `void glworld_submit_line(float x0,y0,x1,y1; uint32_t rgba);` — selection-box / debug lines.
- `void glworld_end_frame(void);` — flush all batches in submission order to the scene FBO. Leaves the truecolor world in the scene texture for the present/composite stage.
- `GLuint glworld_scene_texture(void);` — the truecolor world texture handle for compositing.
- `void glworld_shutdown(void);`

### 4.2 `engine_render.c`
Add `extern TbBool gl_world_active;` checks in the per-primitive draw functions (the thin branches in §3.1). No change to the bucket build, projection, or sort.

### 4.3 `bflib_render_gl.c` (existing)
Extend the present/composite stage to draw the GPU world scene texture first, then composite the 8-bit GUI/overlays on top (transparency for the world region), then run post-FX. Reuse the existing scene FBO / post-FX infrastructure.

### 4.4 `bflib_video.c`
Wire `glworld_init`/`shutdown` alongside the existing `gl_present_*` lifecycle; set `gl_world_active` from `KFX_GLWORLD` (default on when GL present is active) and from init success.

## 5. Texture store (the clean seam to hi-res)

A GPU texture store holds every terrain tile and sprite frame the world references, addressed by the engine's existing texture/sprite ids.
- **Phase 1 (this sub-project):** populated by converting the engine's current paletted tiles (`block_ptrs`) and `KeeperSprite` frames to RGBA using the active palette — pixel-identical to the software look. Animated tiles update their store entry when `block_ptrs` swap.
- **Phase 2 (Sub-project 2):** the same store slots are filled from a hi-res truecolor asset pack instead. **The renderer code does not change between phases** — only the texture source does. This is the seam that makes the hi-res upgrade a drop-in.

Implementation note: a texture array (or atlas) keyed by tile/sprite id; per-frame upload only for animated/changed entries.

## 6. Phasing (within Sub-project 1)

1. **Terrain** (floors/walls) on GPU, palette-converted, matching look. Verify parity on a static scene.
2. **Billboards** (creatures/objects/effects/floating text) + **selection box**, preserving bucket-order occlusion.
3. **GUI/overlay composition** + post-FX integration (the transparent-sentinel composite).
4. **Parity & perf pass:** side-by-side vs software render; confirm visual parity, full-speed, no crashes through a level; tune.

## 7. Fallback, safety, testing

- `KFX_GLWORLD=0` or any `glworld_init`/FBO/shader failure → `gl_world_active=false` → the existing software rasterizer runs (game always works). Total GL failure still hits the existing CPU-blit present fallback.
- **Testing:** primary criterion is **visual parity** with the software renderer (Phase 1 indistinguishable). Then: runs a full level with no crash, no GL errors, full frame rate. Smoke-test via `run-linux.sh -level 1` + log checks (GL world active, no errors, reaches gameplay), plus human side-by-side screenshots (GL world vs `KFX_GLWORLD=0`).

## 8. Constraints

- Native Linux x86-64 (LP64). All GL/world code fenced `#ifndef _WIN32`; the Windows build keeps the software path. Watch the established LP64 hazards (`long` widths, `min(a-b,b-a)` promotion) in any new math.
- `-Werror` clean. Don't modify `/opt`, vendored deps, or the engine's geometry/sim. Edits confined to: new `bflib_render_glworld.{c,h}`, the per-primitive branches in `engine_render.c`, the composite extension in `bflib_render_gl.c`, and lifecycle wiring in `bflib_video.c`.
- Keep the **present-backend abstraction** clean so a Vulkan backend can later implement the same `glworld_*` / `gl_present_*` interface.

## 9. Out of scope (explicitly)

- Hi-res asset generation/loading (Sub-project 2).
- Per-pixel / normal-mapped / colored dynamic lighting; real depth-buffer Z (later refinement).
- GUI/HUD hi-res upgrade.
- Vulkan backend.
- Any gameplay/sim change.

## 10. Risks

- **Composition correctness** (world ↔ 8-bit GUI/overlay transparency) is the fiddliest part — overlays drawn over the world must composite correctly. Mitigation: a well-defined transparent sentinel for the world region; validate with tooltip/menu/hand-over-world cases.
- **Parity** of GPU vs software shading (the per-vertex shade → color mapping must match the palette/shade-table result). Mitigation: match the shade math; pixel-diff against software render.
- **Scope creep** into lighting/3D. Mitigation: this spec hard-scopes to "match current look, truecolor, on GPU."
