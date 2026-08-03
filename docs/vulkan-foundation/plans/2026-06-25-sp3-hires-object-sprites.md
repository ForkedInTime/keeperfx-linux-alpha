# Hi-res Object/Item Sprites Implementation Plan

**Goal:** Render a high-impact set of object/item sprites (dungeon heart, gold, statue, traps, doors, a couple of effects) as hi-res truecolor, through SP1's GPU sprite path, gated by `KFX_HIRES_SPRITES` (off by default), ending in an in-game render the user confirms.

**Architecture:** Mirror the SP2 terrain override seam on SP1's sprite path. Offline: the proven object-upscale pipeline produces `hires_sprites/sprite_<frame_key>.png` for every frame of each chosen object. Runtime: a parallel RGBA8 `GL_TEXTURE_2D_ARRAY` + a `frame_key→layer` lookup; `glworld_submit_keepersprite` tags each sprite command with its override layer (or -1); the sprite fragment shader samples the RGBA8 array (× a shade scalar, honouring `flip_x`) for overridden frames and the unchanged paletted path otherwise.

**Tech Stack:** Python 3 (PIL, numpy) + `realesrgan-ncnn-vulkan` + diffusers/`sdxl-turbo` offline; C + OpenGL 3.3 core (libepoxy) + `spng` runtime. Build: CMake + Ninja.

## Global Constraints

- Linux x86-64 (LP64). All new GL code `#ifndef _WIN32`; the `gl_hires_sprites_active` flag declared OUTSIDE the fence (like `gl_world_active`). `-Werror` clean; BOTH `keeperfx` and `keeperfx_hvlog` build (`cmake --build bin-linux -- -k 0`).
- Opt-in via `KFX_HIRES_SPRITES=1` (+ `KFX_HIRES_SPRITES_DIR`, default `hires_sprites`). Unset (default) or Windows → no override tagged → SP1 sprite path **byte-for-byte identical**.
- Runtime edits confined to `src/src/bflib_render_glworld.{c,h}` + `src/src/bflib_video.c`. No change to engine geometry/projection/sort/sim, the SP1 command-list/occlusion, the terrain paths, or the paletted sprite path when off. Offline tools under `tools/hires_sprites/`; generated `hires_sprites/` is git-ignored. `/opt` read-only.
- Reuse SP1 Task 4 sprite infra (atlas/shader/command list) — the override is additive, on free texture units.

**Verified SP1 facts the tasks build on (from `bflib_render_glworld.c`):**
- Submit: `void glworld_submit_keepersprite(int dx,dy,dw,dh, const void *src_data, size_t src_len, int src_w,int src_h, uint32_t frame_key, const unsigned char *remap, TbBool flip_x, enum GlWorldSpriteBlend blend)`. **`frame_key == kspr_idx`** (the per-frame id; the override key). Called at `engine_render.c:7845`.
- Sprite atlas `gw.spr_atlas_tex` (RG8: R=index, G=coverage), slots keyed by `frame_key` (`struct GlwSprSlot`), `GLW_SPR_CELL=256`, `GLW_SPR_COLS=16`. Sprite program `gw.spr_program`, uniforms `gw.su_pal/su_atlas/su_remap` (set near line 1327). Sprite vertex = `GLW_SPR_VFLOATS=6` floats `sx,sy,u,v,remapRow,(pad)` (line 175); VBO/VAO set near line 1334. The sprite draw is `glw_draw_sprite_cmd` (line 1091), one entry in the ordered command list. `flip_x` mirrors directional frames. Per-frame epoch in `glw_frame_epoch`.
- The terrain override (SP2) already added: a shade-scale LUT `gw.shade_tex` (64×1 RGB32F, per-shade brightness from `pixmap.fade_tables`+`lbPaletteColors`), the `GL_MAX_ARRAY_TEXTURE_LAYERS` clamp pattern, and an spng RGBA loader (`glw_hires_decode_png`). REUSE these.

---

### Task 1: Object extraction + upscale → `hires_sprites/`

Builds `tools/hires_sprites/` and runs it to produce the high-impact object PNGs. Reuses the proven object-upscale stages (from the SP2 `tools/hires` pipeline + the de-risk `sprite_proof.py`). Deliverable: the reusable tool + the generated `hires_sprites/sprite_<frame_key>.png` set + a manifest.

**Files:**
- Create: `tools/hires_sprites/extract_probe.md` (the temp engine-probe recipe — documents the one-shot dumper to add/run/remove; NOT committed engine code)
- Create: `tools/hires_sprites/upscale_sprites.py` (edge_fill → ESRGAN → masked SDXL → color_keep → recombine alpha; batch over `/tmp/objframe_*.png`)
- Create: `tools/hires_sprites/objects.txt` (the high-impact object model list to extract)
- Create: `tools/hires_sprites/README.md`
- Output (git-ignored): `hires_sprites/sprite_<frame_key>.png` (RGBA8) + `hires_sprites/manifest.txt`

**Interfaces:**
- Produces (on disk): `hires_sprites/sprite_<frame_key>.png` keyed by the integer `frame_key` (the `kspr_idx` the engine submits per frame) — consumed by Task 2's loader.
- Produces (Python, importable): `upscale_sprite(rgba: np.ndarray, prompt: str) -> np.ndarray` (RGBA8 hi-res, alpha preserved), reusing `edge_fill`, `color_keep` (LAB luminance-from-AI / chroma-from-original).

- [ ] **Step 1: Object list.** `tools/hires_sprites/objects.txt` — the high-impact models (resolve exact `ObjectModel` enum names from `config_objects.*`/objects.cfg during implementation):
```
# object_model   label
SOUL_CONTAINER   dungeon_heart
GOLD_POT         gold_pot
GOLD_CHEST       gold_chest
GOLDHOARD_1      gold_hoard
TEMPLE_STATUE    statue
TORCH            torch
# + a few traps + doors + 1-2 spell/effect object sprites confirmed visible in early levels
```

- [ ] **Step 2: Extraction probe (temporary engine code, run + remove, NOT committed).** Following `tools/hires_sprites/extract_probe.md`, add a one-shot routine (hook after level 1 starts) that, for each object model in `objects.txt`: resolves its keepersprite anim base (`get_object_model_stats(model)->sprite_anim_idx`), its frame count (`keepersprite_frames(anim_id)`), and for each `frame_num` computes the exact submitted `frame_key` (the `kspr_idx` value `glworld_submit_keepersprite` would receive — match `draw_single_keepersprite`/`process_keeper_sprite`'s `draw_idx`), decodes that frame's RLE to RGBA (RGB from `lbPaletteColors[index]`, alpha=0 for transparent/skip runs + index 0, else 255), and writes `/tmp/objframe_<frame_key>.png` via spng (`SPNG_FMT_PNG` on encode — NOT `SPNG_FMT_RGBA8`, which silently writes 0 bytes). Build, run headless (`KFX_GLWORLD=1 KFX_WINDOWED=1 KFX_POSTFX=0 ./run-linux.sh -nointro -level 1 -pause_at_gameturn 200`), confirm the PNGs exist with sane dims + transparency. **Then remove the probe and rebuild to a clean tree** (the frame PNGs persist in /tmp). Record the `frame_key → object/label/frame` mapping into `hires_sprites/manifest.txt`.

- [ ] **Step 3: Upscale pipeline.** `tools/hires_sprites/upscale_sprites.py` (reuse the de-risk `sprite_proof.py` logic verbatim where possible):
```python
import os, glob, subprocess, numpy as np
from PIL import Image, ImageFilter
import torch, torch.nn as nn
from diffusers import AutoPipelineForImage2Image
ESRGAN="/usr/bin/realesrgan-ncnn-vulkan"; MODELS="/usr/share/realesrgan-ncnn-vulkan/models"
def edge_fill(rgb, alpha, iters=24):
    rgb=rgb.astype(np.float32).copy(); m=(alpha>10).astype(np.float32)
    for _ in range(iters):
        col=rgb*m[...,None]
        cb=np.asarray(Image.fromarray(np.clip(col,0,255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(2)),np.float32)
        wb=np.asarray(Image.fromarray((m*255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(2)),np.float32)/255.0
        fill=np.where(wb[...,None]>1e-3, cb/np.maximum(wb[...,None],1e-3), 0); upd=(m<0.5)&(wb>1e-3)
        rgb=np.where(upd[...,None], fill, rgb); m=np.where(upd,1.0,m)
        if m.min()>0.5: break
    return np.clip(rgb,0,255).astype(np.uint8)
def color_keep(detailed,color,l=0.65):
    d=np.asarray(Image.fromarray(detailed,"RGB").convert("LAB"),np.float32)
    c=np.asarray(Image.fromarray(color,"RGB").convert("LAB"),np.float32); o=c.copy()
    o[...,0]=c[...,0]*(1-l)+d[...,0]*l
    return np.asarray(Image.fromarray(o.clip(0,255).astype(np.uint8),"LAB").convert("RGB"))
# load SDXL-Turbo once; for each /tmp/objframe_<key>.png:
#   edge_fill -> ESRGAN x4 -> square-pad 512 -> img2img(strength~0.30,steps6,guidance0,
#   prompt by object label) -> crop back -> color_keep(l=0.65) -> recombine Lanczos-upscaled alpha
#   -> hires_sprites/sprite_<key>.png
```
Per-label prompts (e.g. heart: "ornate gold altar with glowing red gem, polished, dark fantasy"; statue: "carved gold idol statue, ornate, dark fantasy"; gold: "pile of gold coins, glinting treasure"). Drive via `objects.txt`/manifest. **Process every frame** of each object.

- [ ] **Step 4: Run + verify.** `cd /mnt/Storage/Projects/keeperfx/src && PYTHONPATH=. KFX_HIRES_VENV=<sdxl venv> bash tools/hires_sprites/run.sh` (or invoke `upscale_sprites.py` over `/tmp/objframe_*.png`). Confirm `hires_sprites/sprite_*.png` exist (RGBA, larger than source), alpha clean (build a quick montage on a checker bg and eyeball — recognizable + no halos). Write README documenting the probe + run.

- [ ] **Step 5: Commit** (tools + manifest + README; art git-ignored):
```bash
cd /mnt/Storage/Projects/keeperfx/src
echo "/hires_sprites/" >> .gitignore
git add tools/hires_sprites .gitignore
git commit -m "feat(hires-spr): offline object-sprite upscale pipeline (tools/hires_sprites)"
```

---

### Task 2: Runtime override store + loader (no shader change yet)

**Files:**
- Modify: `src/src/bflib_render_glworld.h` (override interface + `gl_hires_sprites_active`)
- Modify: `src/src/bflib_render_glworld.c` (RGBA8 sprite-override array, `frame_key→layer` lookup, loader, lifecycle)
- Modify: `src/src/bflib_video.c` (`KFX_HIRES_SPRITES` opt-in)

**Interfaces:**
- Produces (header, flag outside the `#ifndef _WIN32` fence):
  - `extern TbBool gl_hires_sprites_active;`
  - inside the fence: `void glworld_hires_sprites_load(const char *dir);` `void glworld_hires_sprites_shutdown(void);` `GLuint glworld_hires_sprites_array(void);` `int glworld_hires_sprites_layer(uint32_t frame_key);` (returns layer ≥0 or -1).
  - `#define GLW_HIRES_SPR_DIM 256` (override cell edge; object frames upscaled to fit; store as a square array layer with the sprite centered or scaled — see note).
- Internals: a `GL_TEXTURE_2D_ARRAY` RGBA8 (`GLW_HIRES_SPR_DIM²` × N layers, `GL_LINEAR`, `GL_CLAMP_TO_EDGE`); an open-addressed hash `frame_key→layer` (sparse over the large id space). Each PNG may be non-square — store it scaled into the layer preserving aspect, and record per-layer UV scale OR (simpler) store each frame in its own array layer sized to a common `GLW_HIRES_SPR_DIM` square with the sprite fit and **carry the original w:h as a per-layer uv-rect** so the shader samples the right sub-region. Simplest correct approach: pad each upscaled frame to a square `GLW_HIRES_SPR_DIM` and store a `vec4 uvrect[layer]` (x,y,w,h in 0..1) the shader uses; OR resize each frame to exactly `GLW_HIRES_SPR_DIM²` (stretch) and have the sprite quad UVs already 0..1 — **pick resize-to-square so UVs map 0..1 directly to the layer** (the sprite quad already uses 0..1 UVs over the frame; the paletted atlas handles non-square via cell sub-rect, but for the override array, resizing each frame to the layer square keeps UVs 0..1 with no per-layer rect). Document the choice; verify aspect looks right in Task 3.

- [ ] **Step 1: Header interface.** Add `extern TbBool gl_hires_sprites_active;` (outside fence) and the load/shutdown/array/layer decls + `#define GLW_HIRES_SPR_DIM 256` (inside fence). Mirror the SP2 `glworld_hires_*` header block.

- [ ] **Step 2: Override store + loader.** In `bflib_render_glworld.c` (inside the fence), define `TbBool gl_hires_sprites_active=false;` at file scope (outside fence). Add a `gw_hspr` state struct `{ GLuint ov_array; struct{uint32_t key;int layer;}*map; int map_cap; int count; }`. Implement:
  - `glworld_hires_sprites_load(dir)`: `glworld_hires_sprites_shutdown()` first; if `!gw.inited||!dir||!dir[0]` return. Scan `dir` for `sprite_<key>.png` (parse `key` via `sscanf(name,"sprite_%u.png",&key)`). Heap-collect matches (dynamic, no fixed cap — mirror the SP2 fix). Clamp to `GL_MAX_ARRAY_TEXTURE_LAYERS` with `LbWarnLog` on drop. Create `ov_array` = `glTexImage3D(GL_TEXTURE_2D_ARRAY,0,GL_RGBA8,GLW_HIRES_SPR_DIM,GLW_HIRES_SPR_DIM,N,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL)`. For each file: decode via the existing `glw_hires_decode_png`-style spng loader **but generalize it to accept the PNG's own WxH** (Task 1 stores arbitrary sizes; either resize on CPU to `GLW_HIRES_SPR_DIM²` before upload, or decode-then-resize) → `glTexSubImage3D(...,layer,...)`; insert `key→layer` into the open-addressed map. `GL_LINEAR`/`GL_CLAMP_TO_EDGE`, optional mipmaps. `gl_hires_sprites_active = (count>0)`. `glworld_check_error("hires_sprites_load")`.
  - `glworld_hires_sprites_layer(key)`: hash lookup → layer or -1.
  - `glworld_hires_sprites_shutdown()`: delete `ov_array`, free `map`, reset.
- [ ] **Step 3: Lifecycle wiring.** Call `glworld_hires_sprites_shutdown()` in `glworld_shutdown`. In `bflib_video.c`, read `KFX_HIRES_SPRITES`/`KFX_HIRES_SPRITES_DIR` (mirror the `KFX_HIRES` block) and call `glworld_hires_sprites_load(dir)` after `glworld_init` when enabled.
- [ ] **Step 4: Build both targets + headless smoke.** `cmake --build bin-linux -- -k 0` clean. Run `KFX_GLWORLD=1 KFX_HIRES_SPRITES=1 KFX_HIRES_SPRITES_DIR=/mnt/Storage/Projects/keeperfx/src/hires_sprites KFX_WINDOWED=1 ./run-linux.sh -nointro -level 1 -pause_at_gameturn 120`; log shows N sprite overrides loaded, `gl_hires_sprites_active=1`, 0 GL errors, reaches gameplay. `KFX_HIRES_SPRITES=0` → no load, unchanged. (No visual change yet.)
- [ ] **Step 5: Commit.** `git add src/src/bflib_render_glworld.{c,h} src/src/bflib_video.c && git commit -m "feat(hires-spr): GPU sprite override store + loader + KFX_HIRES_SPRITES gating"`

---

### Task 3: Sprite-shader override branch + shade

**Files:** Modify `src/src/bflib_render_glworld.c` (sprite vertex+fragment shader, a per-vertex override-layer attribute, binding in the sprite draw, shade reuse).

**Interfaces:** Consumes Task 2's `glworld_hires_sprites_layer/array` and `gl_hires_sprites_active`; SP1's `gw.spr_program`, the sprite vertex format (`GLW_SPR_VFLOATS`), the command-list sprite path, and the SP2 `gw.shade_tex` shade LUT.

- [ ] **Step 1: Tag the override layer at submit.** In `glworld_submit_keepersprite`, compute `int ov = gl_hires_sprites_active ? glworld_hires_sprites_layer(frame_key) : -1;` and store it on the sprite command (extend the `GlwCmd` sprite entry with `float ov_layer`), so it reaches `glw_draw_sprite_cmd`. Add the override layer as a new sprite vertex attribute (bump `GLW_SPR_VFLOATS` 6→7, add `aOvLayer` at the next location, write `ov` into each of the 6 verts). Keep `-1` when no override.
- [ ] **Step 2: Sprite shader override branch.** Add to the sprite fragment shader: `uniform sampler2DArray uSprOv; uniform sampler2D uShade;` and `flat in float vOvLayer;`. Branch:
```glsl
if (vOvLayer >= 0.0) {
    vec4 hi = texture(uSprOv, vec3(vUV, vOvLayer));   // vUV already 0..1 over the frame; flip handled in vertex UV
    if (hi.a < 0.02) discard;
    float sc = texelFetch(uShade, ivec2(vShadeLevel,0),0).r;   // per-sprite shade scalar (see Step 3)
    oColor = vec4(hi.rgb * sc, hi.a * uAlpha);
} else { /* existing paletted atlas.R -> remap -> palette -> coverage path, unchanged */ }
```
Cache `uSprOv`/`uShade` uniform locations after link. Honour `flip_x`: the sprite path already flips the paletted UV for `flip_x`; ensure the override samples the same flipped `vUV`.
- [ ] **Step 3: Shade scalar for sprites.** The paletted sprite shades via its remap row (`lbSpriteReMapPtr` → a `fade_tables` row when `Lb_TEXT_UNDERLNSHADOW`). Derive a brightness scalar consistent with that: pass the sprite's shade level (or remap-row index) as a vertex attribute and index `gw.shade_tex` (reuse SP2's 64-entry LUT). If the remap row isn't a plain shade row (tint cases: white/red hit-flash), fall back to scalar 1.0 (full bright) so tints don't double-darken. Document the mapping; verify the heart/gold read correct vs the paletted version across light/shadow.
- [ ] **Step 4: Bind + build + headless visual check.** Bind `uSprOv` (override array) + `uShade` on free texture units in the sprite draw; set `vShadeLevel`. Build both targets `-Werror` clean. `KFX_HIRES_SPRITES=1` FBO dump shows the overridden objects (heart etc.) rendered hi-res with correct placement/scale/alpha; `=0` identical to SP1; both POSTFX on/off; no GL errors; objects animate without hi-res/paletted flicker (all frames overridden). If an object renders wrong-aspect or mis-placed, fix the UV/layer mapping before committing.
- [ ] **Step 5: Commit.** `git add src/src/bflib_render_glworld.c && git commit -m "feat(hires-spr): sprite shader override branch + shade (hi-res objects visible)"`

---

### Task 4: In-game visual gate + fallback hardening

**Files:** Modify any of the above for fixes found.

- [ ] **Step 1: Fallback sweep.** Missing dir → no load, paletted, no crash. Corrupt PNG → that frame skipped, others load. `>GL_MAX_ARRAY_TEXTURE_LAYERS` → clamp+log (code-confirm). `KFX_HIRES_SPRITES=0` → scene-FBO identical to SP1. Capture evidence.
- [ ] **Step 2: Build both targets `-Werror` clean.** `cmake --build bin-linux -- -k 0 2>&1 | grep -E 'error:|warning:'` empty.
- [ ] **Step 3: In-game visual comparison (the gate).** Render the same scene `KFX_HIRES_SPRITES=1` vs `=0` (POSTFX on), capture both (window screenshot or full-res `glworld_debug_dump`), build a side-by-side + a zoomed crop on the dungeon heart. The controller sends these to the user. **Do NOT mark SP3 complete until the user confirms the look.**
- [ ] **Step 4: Spec status + commit.** After user confirmation, update the SP3 spec status line; `git add -A && git commit -m "feat(hires-spr): in-game object render + fallback hardening; SP3 objects confirmed"`.

---

## Notes for implementers
- `hires_sprites/` is the seam — adding more objects later is "drop more `sprite_<key>.png`", no renderer change.
- Off-path (`KFX_HIRES_SPRITES` unset) MUST be byte-identical: `ov=-1` everywhere → the new shader `if` is never taken; the new attribute/uniforms are inert.
- Reuse, don't duplicate: the spng loader, the shade LUT (`gw.shade_tex`), and the `GL_MAX_ARRAY_TEXTURE_LAYERS` clamp+log already exist from SP2 — call them.
- Don't touch creatures or terrain. Don't change the paletted sprite path, the command-list ordering, or occlusion.
