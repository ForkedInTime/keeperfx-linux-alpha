# Sub-project 3 — Hi-res Object/Item Sprites — Design Spec

**Status:** Approved direction (user chose "build the objects pipeline", 2026-06-25), pre-implementation
**Branch:** `linux-native-port`
**Depends on:** SP1 (GPU world renderer) — COMPLETE, incl. Task 4 which already renders world sprites on the GPU via a paletted RG8 sprite atlas. This sub-project adds a hi-res RGBA8 override on top of that atlas, exactly mirroring the terrain hi-res override (SP2) but on the sprite path.

## 1. Goal

Replace the engine's low-res paletted **object/item** sprites (dungeon heart, gold, statues, traps, doors, room items, spell/effect sprites) with **hi-res truecolor** versions, authored offline, rendered through SP1's GPU sprite path, gated by an opt-in flag and **off by default** (byte-identical to SP1 when off). Creatures are explicitly OUT (proven non-automatable — see §8). This completes the *automated* graphics modernization: GPU renderer + post-FX + hi-res terrain(optional) + hi-res objects.

### Why objects (validated by de-risk proofs, 2026-06-25)
Object sprites upscale cleanly to a genuine "remastered" look (the dungeon heart 83×95, temple statue 62×107, gold pot all proved out): they are large enough and detailed enough that ESRGAN+SDXL recovers real detail, they are **unique single images** (no tiling/striping like terrain), and most are **single-view** (not 8-directional), so there is no animation-consistency problem like creatures. Transparency upscales cleanly via edge-fill + separate alpha upscale.

### Success = in-game visual confirmation
The defining success criterion (per the project's standing rule): the user sees the hi-res objects rendered **in-game** (`KFX_HIRES_SPRITES=1` vs `=0`) — the dungeon heart, gold, and other objects visibly remastered — and confirms the look. A passing build is not acceptance; the rendered frame is.

## 2. Background (engine facts this design relies on)

- **Sprite frames:** `KeeperSprite` frames, RLE-encoded paletted. Decoded in `engine_render.c` (`sprite_to_sbuff`, `draw_keepersprite`). A frame's drawn id is `draw_idx = kspr_idx + sprite_rot*FramesCount + frame_num` (`draw_single_keepersprite*`). Objects resolve via `get_object_model_stats(model)->sprite_anim_idx` (the keepersprite anim base) + `keepersprite_frames(anim_id)` (frame count); `keepersprite_rotable(anim_sprite)` says whether directional (most objects are NOT). `OBJECT_TYPES_MAX` object models; the high-impact ones: SOUL_CONTAINER (dungeon heart), gold (pot/chest/hoard), TEMPLE_STATUE, traps, doors, lair, hatchery egg, torches, spell/special effects.
- **SP1 GPU sprite path** (`bflib_render_glworld.c`, Task 4): the world-sprite blit is intercepted at `draw_keepersprite` (engine_render.c:7845) and submitted via
  `glworld_submit_keepersprite(int dx,dy,dw,dh, const void *src_data, size_t src_len, int src_w,int src_h, uint32_t frame_key, const unsigned char *remap, TbBool flip_x, enum GlWorldSpriteBlend blend)`.
  **`frame_key` = the frame's `kspr_idx`** (the exact per-frame id). Frames are decoded once into a **RG8 sprite atlas** (`gw.spr_atlas_tex`, R=palette index, G=coverage), cached in an open-addressed slot map keyed by `frame_key` (`struct GlwSprSlot{key,w,h,...}`), `GLW_SPR_CELL=256`, `GLW_SPR_COLS=16`, dynamic rows clamped to GL limits. The **sprite fragment shader** (`gw.spr_program`, uniforms `uAtlas`/`uRemap`/`uPal`) resolves `atlas.R index → remap[index] (uRemap row = shade/tint) → palette → rgb`, coverage→alpha, `uAlpha` blend. `flip_x` mirrors directional frames. The sprite draw is one entry in the ordered command list (preserves occlusion vs terrain).
- **`frame_key` is the stable override key.** It is available at submit time and is exactly what identifies a hi-res replacement. (The dynamic atlas *slot* evicts, but `frame_key` does not — key the override by `frame_key`, not slot.)

## 3. Architecture — mirror the terrain seam, on the sprite path

```
OFFLINE (Python, dev GPU; reuses the proven object-upscale pipeline)
  per chosen object model -> enumerate its KeeperSprite frame_keys (base + each frame)
    -> temp engine probe decodes each frame to RGBA PNG (RLE -> palette, alpha from skip-runs)
    -> edge-fill (spread opaque colour into transparent, no ESRGAN halos)
    -> ESRGAN x4 -> masked SDXL detail (strength ~0.30) -> color-keep (LAB: keep orig a/b, AI luminance)
    -> recombine upscaled alpha
    -> write hires_sprites/sprite_<frame_key>.png  (RGBA8)        <- this folder IS the seam
RUNTIME (C, bflib_render_glworld.{c,h}; only when KFX_HIRES_SPRITES=1)
  load hires_sprites/*.png -> RGBA8 GL_TEXTURE_2D_ARRAY (one layer per frame_key)
                            + frame_key -> layer lookup (open-addressed map / hash)
  in glworld_submit_keepersprite: if frame_key has an override, tag the sprite command with
    its override layer (else -1).
  sprite fragment shader: override layer >= 0 ?
     sample RGBA8 array (LINEAR, UV, flip_x) * shadeScale  -> use its rgb+alpha   (hi-res path)
   : existing atlas.R -> remap -> palette -> coverage alpha                         (unchanged SP1 path)
```

The two halves meet only at the `hires_sprites/` PNG folder, keyed by `frame_key`. Off (default) → no override layer ever tagged → SP1 path byte-identical.

## 4. Components & interfaces

### 4.1 Offline (`tools/hires_sprites/`, not compiled in)
- Reuse a **temp engine probe** (the de-risk proofs already proved the decode; productionize a one-shot dumper that, for a configured list of object models, resolves every `frame_key` it can draw and writes `sprite_<frame_key>.png` RGBA8). Removed after extraction; not committed.
- `upscale_sprites.py`: the proven pipeline — `edge_fill` → ESRGAN `realesrgan-x4plus` → square-pad → SDXL-Turbo img2img (strength ~0.30, circular padding unnecessary for non-tiled sprites) → `color_keep` (LAB luminance from AI, a/b from original) → recombine upscaled alpha → `hires_sprites/sprite_<frame_key>.png`. A manifest lists `<frame_key> <object_model> <label>`.
- **Override every frame** of each animated object (heart pulse, gold glint, torch flicker) so it never flickers between hi-res and paletted.

### 4.2 Runtime (`bflib_render_glworld.{c,h}`)
- `extern TbBool gl_hires_sprites_active;` (declared outside the `#ifndef _WIN32` fence, like `gl_world_active`).
- `void glworld_hires_sprites_load(const char *dir);` — scan `dir` for `sprite_<frame_key>.png`, decode (spng → RGBA8, `SPNG_FMT_PNG` on encode is the offline gotcha; decode is `SPNG_FMT_RGBA8`), upload into a `GL_TEXTURE_2D_ARRAY` (RGBA8, `GL_LINEAR`, `GL_CLAMP_TO_EDGE`, mipmaps optional), build a `frame_key→layer` lookup (open-addressed hash, since `frame_key` spans a large id space and the override set is sparse). Clamp layer count to `GL_MAX_ARRAY_TEXTURE_LAYERS`, log drops. No-op when dir missing/`!gl_hires_sprites_active`.
- `void glworld_hires_sprites_shutdown(void);` / accessors `glworld_hires_sprites_array()`, `glworld_hires_sprites_layer(uint32_t frame_key)` (returns layer or -1).
- In `glworld_submit_keepersprite`: `int ov = gl_hires_sprites_active ? glworld_hires_sprites_layer(frame_key) : -1;` and carry `ov` (+ a flip flag) into the sprite command/vertex attribute.
- **Sprite shader**: add `sampler2DArray uSprOv`, an `aOvLayer` attribute (float; -1 = none), and (reuse) the shade-scale LUT `uShade`. Branch: `if (aOvLayer >= 0.0) { vec4 hi = texture(uSprOv, vec3(uv_flipped, aOvLayer)); rgb = hi.rgb * shadeScale; a = hi.a * uAlpha-blend; } else { existing paletted path; }`. `uv_flipped` honours `flip_x`. Shade: derive a per-sprite brightness scalar from the sprite's remap row (the remap already encodes shade); v1 may approximate by the remap-row average luminance, or render objects at full brightness if they read as self-lit — pick the one that matches the paletted look; verify against side-by-side.
- Lifecycle: load after `glworld_init` when the env opt-in is set (mirror `glworld_hires_load`); free in `glworld_shutdown`; rebuild on re-init.

### 4.3 `bflib_video.c`
- Read `KFX_HIRES_SPRITES` (and `KFX_HIRES_SPRITES_DIR`, default `hires_sprites/`) alongside the existing `KFX_GLWORLD`/`KFX_HIRES` env handling; call `glworld_hires_sprites_load` only when enabled and the GL world initialised.

## 5. Fallback, safety, testing

- **Zero-regression:** `KFX_HIRES_SPRITES` unset (default) or on Windows → no override layer tagged → SP1 sprite path byte-identical. Missing dir / corrupt PNG → that frame stays paletted (per-frame fallback). Over `GL_MAX_ARRAY_TEXTURE_LAYERS` → clamp + log. `/opt` read-only; generated art in a separate opt-in folder; stock data untouched.
- **Offline:** each upscaled object reads as itself (recognizable), alpha clean (no halos), color faithful (gold stays gold). Captured as before/after.
- **Runtime (headless-capable):** `KFX_HIRES_SPRITES=0` → scene-FBO identical to SP1; `=1` → overridden objects render hi-res with correct placement/scale/alpha, animate without flicker (all frames overridden), no GL errors, full level. `-Werror` clean, both targets.
- **Final (human):** in-game `=1` vs `=0` — the heart and objects visibly remastered; user confirms. This is acceptance.

## 6. Constraints

- Linux x86-64 LP64; all new GL code `#ifndef _WIN32`; `gl_hires_sprites_active` flag outside the fence. `-Werror` clean; both `keeperfx` + `keeperfx_hvlog`.
- Runtime edits confined to `bflib_render_glworld.{c,h}` + `bflib_video.c`. No change to engine geometry/projection/sort/sim, the SP1 command-list/occlusion, the terrain paths, or the paletted sprite path when off. Offline tools under `tools/hires_sprites/`; generated `hires_sprites/` is git-ignored.
- Reuse SP1's sprite atlas/shader/command-list infrastructure; the override is additive. Use free texture units (beyond those Task 4 uses).

## 7. Phasing (within SP3)

1. **Object extraction + upscale** → `hires_sprites/` for a high-impact first set (heart, gold, statue, traps, doors, a couple of effects). (Pipeline proven; this is enumeration + batch.)
2. **Runtime override store + loader** (RGBA8 array + `frame_key→layer` lookup + `KFX_HIRES_SPRITES` gating). No shader change yet — verifiable in isolation.
3. **Sprite-shader override branch + shade** (objects render hi-res). The visible step.
4. **In-game visual gate + fallback hardening** — render `=1` vs `=0`, user confirms; verify fallbacks; final review.

## 8. Out of scope (explicitly)

- **Creatures** (proven non-automatable for animation: single frames are AAA but multi-direction/multi-frame regen morphs — needs real character art / 3D). Future project with an art budget.
- **De-striped terrain** (needs renderer per-tile variation — future engineering project).
- GUI/HUD hi-res, normal/PBR maps, Vulkan, any gameplay change.

## 9. Risks

- **Animated-object flicker** if not all frames overridden → mitigation: enumerate + override every frame of each object; fall back per-frame to paletted only for genuinely-missing frames.
- **Shade mismatch** (hi-res multiply vs the paletted remap-row shade) at object edges/in shadow → mitigation: derive the multiplier from the same remap/fade data; verify side-by-side; objects that are self-lit (heart, gold glow) are forgiving.
- **`frame_key` collisions / wrong frame** → key strictly by the `frame_key` the engine submits (the proofs confirmed the decode matches); verify the heart's frames map correctly in-game.
- **Scope creep into creatures/terrain** → hard-scoped to object/item sprites; §8 holds.
