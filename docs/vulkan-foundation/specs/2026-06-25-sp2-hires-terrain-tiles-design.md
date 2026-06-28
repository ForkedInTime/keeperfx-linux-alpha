# Sub-project 2 — Hi-res Truecolor Terrain Tiles (Proof Pack) — Design Spec

**Status:** Proof pack rendered in-game and visually confirmed by user (2026-06-25). Fallback hardening complete. Next step: scale to full terrain tileset.
**Branch:** `linux-native-port`
**Date:** 2026-06-25
**Depends on:** Sub-project 1 (truecolor GPU world renderer) — COMPLETE. Provides the texture-store seam this work plugs into.

## 1. Goal

Replace a small set (~8–12) of the highest-visual-impact 8-bit terrain tiles with **hi-res truecolor** versions, authored **offline**, and render them through the SP1 GPU world renderer so the player sees a real material upgrade (detailed stone / masonry / gold / lava) while the engine, geometry, lighting, and gameplay are unchanged. This is a **proof pack**: the deliverable is an in-game, side-by-side verdict on whether hi-res art lifts the look enough to justify authoring the full tileset (and later, sprites).

A pre-implementation visual proof on the real game tiles (TMAPA000) confirmed the look is achievable: faithful upscaling alone is a modest de-pixelation, but **ESRGAN upscale + a tile-aware low-strength SDXL detail pass (seamless via circular-padding convolutions) + a palette-match step** produces a genuine, seamless, on-material transformation. This spec builds that pipeline and the renderer path to display it.

### Success criteria
- The proof-pack tiles render in-game (GPU world path, `KFX_HIRES=1`) as hi-res truecolor, correctly placed, seamlessly tiled, and **shaded by the engine's existing lighting** (torchlight / dynamic cursor-light still darken/brighten them).
- Non-overridden tiles and the entire game are **unchanged**; `KFX_HIRES` off (default) is byte-for-byte identical to SP1.
- Full level runs with no crash, no GL errors, full frame rate.
- A human side-by-side (`KFX_HIRES=1` vs `=0`) is the final verdict; this folds in the SP1 visual sign-off deferred from that sub-project.

## 2. Background (facts this design relies on)

- **Tile format:** terrain tiles are **32×32 8-bit paletted**, 1024 bytes each, concatenated in `tmap{a,b}NNN.dat`. These files are **RNC ProPack (`RNC\x01`) compressed**; the engine's `LbFileLoadAt` (`bflib_dernc.c: rnc_unpack`) auto-decompresses them into `block_mem`. `TMAPA000.DAT` decompresses to 557056 bytes = **544 'A' tiles** (`TEXTURE_BLOCKS_STAT_COUNT_A = 544`). Data lives in `/opt/keeperfx-personal/DATA/` (symlinked into the run dir by `run-linux.sh`).
- **World palette:** `DATA/PALETTE.DAT` (RNC-compressed → 768 bytes, 256×3, 6-bit VGA scaled to 8-bit) is the in-level palette → `lbPaletteColors`.
- **Block-id → material (from the visual proof on TMAPA000):** lava ≈ blocks 56–71, gold seam ≈ 80–95, smooth path/floor ≈ 48–55, brick wall ≈ 8–23, earth/rock ≈ 36–43. The authoritative slab→block mapping is data-driven in `config/fxdata/cubes.cfg` (cube faces → block indices); exact proof-pack ids are finalized during planning by cross-referencing `cubes.cfg` + a contact-sheet of the active tileset.
- **Animated tiles:** lava/water animate via `update_animating_texture_maps()` swapping `block_ptrs[]` across `TEXTURE_BLOCKS_ANIM_FRAMES = 8` frames; SP1 marks the GPU store dirty there. **The proof pack uses STATIC tiles only** (a single representative lava frame may be overridden as a static look; full animated-lava override is out of scope — deferred).
- **SP1 renderer seam:** `glworld_texstore_sync()` (`bflib_render_glworld.c`) builds an **R8UI index atlas** from `block_ptrs[]`; the terrain fragment shader resolves `index → fade_tables[shade] → palette` (the `fade_tables` LUT is 256×64, `lbPaletteColors` is 256×1). Tiles are addressed by `block_id` → atlas cell. This is the single substitution point.
- **Tooling (present on this host):** `realesrgan-ncnn-vulkan` (model `realesrgan-x4plus`) at `/usr/bin`; a diffusers venv at `scratchpad/assetproof/.venv` with `stabilityai/sdxl-turbo` cached; a standalone RNC decompressor built from the engine's `bflib_dernc.c` (proof artifact in `scratchpad/tileproof/`).

## 3. Architecture — two decoupled halves

```
OFFLINE (Python, runs on the dev GPU, never in the game loop)
  tmapaNNN.dat (RNC) ──dernc──► 32×32 paletted ──palette──► 32×32 RGB
        │
        ├─ 3×3 wrap-tile ─► Real-ESRGAN x4 ─► crop center ─► seamless 4× base (128²)
        ├─ SDXL-Turbo img2img (circular-padding convs, low-strength) ─► detailed, natively tileable (512²)
        ├─ palette-match (Reinhard LAB transfer toward the source tile) ─► color aligned to DK palette
        └─ write hires/tmap_<variation>_<blockid>.png  (RGBA8, 256×256)
                                          │  (this folder IS the interface)
RUNTIME (C, in the renderer; only when KFX_HIRES=1)
  load hires/*.png ─► RGBA8 override atlas + block_id→cell LUT (0xFFFF = none)
        │
  terrain fragment shader: tile has override?
        ├─ yes: sample RGBA8 atlas (0..1 UV, GL_LINEAR) × shade-scale[shade]   ← truecolor path
        └─ no : existing index → fade_tables[shade] → palette                  ← unchanged SP1 path
```

The two halves meet only at the `hires/` PNG folder. Art can be regenerated without touching the engine; engine support can change without touching the art.

## 4. Offline authoring pipeline

A small Python toolset under `tools/hires/` (not compiled into the game). Stages, each independently runnable:

1. **`dernc`** — standalone RNC decompressor compiled from `src/src/bflib_dernc.c` (the engine's own algorithm; build recipe captured in the proof). Decompresses `tmapaNNN.dat` and `PALETTE.DAT`.
2. **`extract_tiles.py`** — decode chosen `block_id`s to 32×32 RGB PNGs using the decompressed palette (6-bit→8-bit scale). Also emits a labeled contact sheet for picking ids.
3. **`upscale_seamless.py`** — for each tile: tile 3×3 (96²) → Real-ESRGAN `realesrgan-x4plus` ×4 (384²) → crop center 128² = seamless 4× base.
4. **`detail.py`** — SDXL-Turbo img2img with **all UNet+VAE `Conv2d` set to `padding_mode='circular'`** (native tiling), per-material prompt, `strength≈0.55`, `steps≈7`, `guidance=0`, fixed seed; base = 128²→512² Lanczos; output 512² detailed, natively tileable.
5. **`palette_match.py`** — **Reinhard LAB color transfer**: shift each detailed tile's per-channel LAB mean/std toward its source 32×32 tile, so the detailed result stays in the DK palette's color family (reduces the warm/saturation drift) while keeping the added structure. (Tunable blend 0..1; default ~0.7 toward source.)
6. **`pack.py`** — downscale to the target cell size (256²), write `hires/tmap_<variation>_<blockid>.png` (RGBA8; alpha = 255 for terrain). A manifest `hires/manifest.txt` lists `<blockid> <material> <source_tile>`.

The proof pack targets ~8–12 static tiles spanning: path/floor, claimed floor, brick wall (+1 variant), earth/dirt, gold seam, rock, and 1–2 room floors; one static lava look optional. Per-material prompts and the exact id list are finalized in the plan.

## 5. Runtime override path (renderer)

All in `bflib_render_glworld.{c,h}` + a small loader; opt-in via `KFX_HIRES=1` and a `hires/` dir resolved next to the game data. **No change to SP1 behavior when off.**

- **Override atlas:** a second GL texture, **RGBA8**, holding the loaded hi-res cells (256×256 each), laid out like the tile atlas; sized within runtime `GL_MAX_TEXTURE_SIZE` (reuse SP1's clamping discipline). `GL_LINEAR` sampling.
- **Override lookup:** `block_id → override_cell`, uploaded as an `R16UI` texture (`0xFFFF` = no override). For ~10 tiles this is tiny; full-size lookup over the tile id space is still trivial.
- **Loader:** at texstore build (and only when `KFX_HIRES=1`), scan `hires/` for `tmap_<var>_<id>.png`, decode (reuse the engine's PNG/`spng` path), upload into the override atlas, set the lookup entry. Missing/corrupt file → that id stays unmapped (paletted fallback).
- **Shade-as-multiply:** precompute, from `fade_tables` + `lbPaletteColors`, a per-shade-level brightness scale (0..63 → scalar or RGB) = the average luminance ratio of `palette[fade_tables[s]]` vs the full-bright row. Upload as a small LUT. The truecolor path outputs `rgba_hires.rgb * shadeScale[shade]`, so engine lighting (static + torch + dynamic cursor light) plays across hi-res tiles consistently with the paletted ones.
- **Shader branch:** in the terrain fragment shader, fetch `override_cell` for the tile id; if valid, sample the RGBA8 atlas at the same 0..1 tile UV and multiply by `shadeScale[shade]`; else run the existing `index → fade → palette` chain. UV semantics and the command-list/occlusion path from SP1 are untouched.
- **Animated tiles:** if an overridden id is an animated block, honor the static override but **skip per-frame swapping for it** (log once); the proof pack avoids animated ids anyway.

## 6. Fallback, safety, testing

- **Zero-regression guarantee:** `KFX_HIRES` unset → override lookup empty → identical to SP1 (and to stock when `KFX_GLWORLD` also off). Override-atlas alloc/over-limit failure → disable the override path, log, continue on the paletted path. Per-file decode failure → that tile falls back to paletted.
- **Offline tests:** each tile (a) tiles seamlessly — 3×3 montage shows no seam discontinuity; (b) still reads as its material; (c) palette-match keeps it in the DK color family. Captured as comparison PNGs.
- **Runtime tests (headless-capable):** `KFX_HIRES=0` → byte-identical scene-FBO dump vs SP1; `KFX_HIRES=1` → overridden tiles render hi-res at correct placement/UV, shade responds to lighting, non-overridden tiles unchanged, full level reached, 0 GL errors, no crash. `-Werror` clean, both targets build.
- **Final verdict (human, deferred):** in-game side-by-side `KFX_HIRES=1` vs `=0` at the user's resolution — does the proof pack justify scaling to the full tileset? This is also where the SP1 visual sign-off lands.

## 7. Constraints

- Native Linux x86-64 (LP64). All new GL/renderer code fenced `#ifndef _WIN32`; Windows build keeps the paletted path. `-Werror` clean. Watch SP1's LP64 hazards.
- Engine edits confined to the renderer seam: `bflib_render_glworld.{c,h}` (+ the override loader), and minimal wiring (`bflib_video.c` for the `KFX_HIRES` lifecycle if needed). **No change** to geometry/projection/sort/sim, the SP1 command list/occlusion, or the GUI/sprite paths.
- Offline tools live under `tools/hires/` and `/opt` assets are **read-only** — never modified. Generated `hires/` art is a separate, opt-in asset folder.
- Keep the texture-store abstraction intact so a future full tileset (and sprites) reuse the same override path with no renderer change.

## 8. Out of scope (explicitly)

- The full terrain tileset and creature/object sprite upgrades (future sub-projects; this is a proof pack).
- Animated hi-res tiles (lava/water per-frame), GUI/HUD hi-res, normal/PBR maps, per-pixel relighting, Vulkan.
- Any gameplay/sim/balance change. Authoring the art into the shipped game-data files (`tmap*.dat`) — overrides load from the separate `hires/` folder, leaving stock data untouched.

## 9. Risks

- **Tiling repeat visibility** (one tile reused across many cells, e.g. gold "columns"). Mitigation: circular-padding native tiling (no seams), optional 2–3 variants per material, and reliance on the angled camera + lighting; the original game has the same property. Flag if a proof tile reads as obviously repetitive in-game.
- **Color/identity drift** from the generative pass. Mitigation: the palette-match (Reinhard LAB) step + per-material prompts + fixed seeds; verify each tile still reads as its material before packing.
- **Lighting mismatch** (truecolor multiply vs paletted fade chain). Mitigation: derive `shadeScale` from the actual `fade_tables`/palette so brightness tracks the paletted path; compare a shaded hi-res tile against its paletted equivalent across shade levels.
- **Scope creep** into the full tileset or animation. Mitigation: hard-scope to ~8–12 static tiles + the override path; everything else deferred behind the in-game verdict.
