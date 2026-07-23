# Render-Scale via `pixel_size` (Integer World Downscale) — Design

**Date:** 2026-07-23
**Status:** Approved design, pre-implementation
**Branch:** `feature/render-scale-pixelsize` (off `alpha`)
**Supersedes:** the parked arbitrary-surface-scaling attempt (`feature/gl-render-scale`), which produced a black world (`geometry_bytes=0`) because it fought the isometric projection.

## Problem / motivation

The 8-bit software rasterizer draws the whole scene at native resolution (~5 M px at 3440×1440) every frame; the 3D terrain rasterization is the CPU bottleneck. We want an optional internal render-downscale so the CPU draws fewer pixels, as a performance lever.

The parked attempt scaled an *arbitrary* sub-native surface and upscaled via GL — but the isometric renderer's view-range math (`compute_cells_away` → the perspective projection) is tuned for the native resolution/zoom relationship and returned an **empty** visible-cell range at an arbitrary scale → black world. That's deep, fragile 25-year-old projection code.

## Key insight (verified)

The engine has a **built-in integer render-downscale: `pixel_size`**. The isometric projection and the GUI are *written* to handle it — every relevant computation divides by `pixel_size`. It shipped working historically for low-res displays, then was hardcoded to `pixel_size = 1`. **Verified 2026-07-23:** forcing `pixel_size=2` renders the world correctly (headless probe: `geometry_bytes ≈ 472 KB`, `cells_away = 42` — a valid view range), versus `0` for the arbitrary-scaling attempt. So render-scale = re-enable `pixel_size`, not fight the projection.

## Approach A (chosen): scope `pixel_size` to the in-game screen

`pixel_size = render_scale` for the **game** screen; `pixel_size = 1` for the **frontend/menu** and movies. The menu↔game transition re-runs the distinct video-setup paths, so the scale flips automatically. This reuses the engine's whole `pixel_size` mechanism (world + in-game HUD + mouse), which historically worked end-to-end.

### Why the crude test broke the menu (and how the real design avoids it)
The throwaway test forced `pixel_size=2` **globally** and left the GUI unit-scaling deriving from the **physical** width while the world rendered at logical (half) resolution — an inconsistency, so the menu drew into the top-left quadrant. The real design (a) scopes it to the game, and (b) derives **all** GUI scaling from the **logical** dims (`physical / pixel_size`), keeping everything self-consistent — exactly how `pixel_size=2` worked historically.

## Components

### 1. Setting — `settings.render_scale`
- New `unsigned char render_scale` in `struct GameSettings` (`config_settings.h`), persisted in `keeperfx.cfg`/`settings.toml` following the `gamma_correction` pattern.
- **Integer pixel-size factor:** `1` = native/off (**default**), `2` = half-res, `3` = third-res. Clamp to `1..3`.

### 2. Scope helper — `active_game_pixel_size()`
- Returns `settings.render_scale` when the **in-game** screen is being set up, else `1`.
- A module-level flag in `vidmode.c` (e.g. `game_screen_active`) set true by the game video-setup path (`reenter_video_mode`/`setup_screen_mode` for gameplay) and false by the frontend/movie paths (`setup_screen_mode_zero`, movie setup). `update_screen_mode_data` reads it.
- Non-GL CPU-blit fallback: unaffected — `pixel_size` scaling is orthogonal to the present method and works on the software surface directly.

### 3. Core change — `update_screen_mode_data` (`vidmode.c`), made self-consistent
- `pixel_size = active_game_pixel_size();` (was hardcoded `1`).
- `MyScreenWidth = width;` / `MyScreenHeight = height;` (physical/surface — identical to today at `pixel_size=1`, where `width*psize == width`).
- Introduce `long lw = width / pixel_size;` `long lh = height / pixel_size;` (**logical** dims), and derive **from `lw`/`lh`** (not `width`/`height`): `units_per_pixel`, `units_per_pixel_min/width/height/best`, `units_per_pixel_menu_height/menu`, `calculate_aspect_ratio_factor`, `calculate_landview_upp`. The `LbScreenSetGraphicsWindow`/`LbTextSetWindow` already divide by `pixel_size`, so they're unchanged.
- **Byte-identity guarantee:** at `pixel_size=1`, `lw==width`, `lh==height`, and `MyScreenWidth==width` — every derived value equals today's. Default users and the entire menu are unaffected.

### 4. Surface — unchanged
The GL/CPU surface stays at native mode size. The world + in-game GUI render at logical res and the low-level draw writes `pixel_size × pixel_size` blocks to fill the native surface (existing `pixel_size` behavior). GL presents 1:1 — no arbitrary surface, no GL upscale.

### 5. Mouse
At `pixel_size≥2`, surface == window == native (unlike the parked branch), so no window↔surface remap is needed. The engine's own `pixel_size` cursor path (logical↔physical) — historically working — should handle it. **This is validation point #1.**

## Data flow
```
keeperfx.cfg render_scale (1/2/3)
  -> game screen setup sets game_screen_active=true
  -> update_screen_mode_data: pixel_size = render_scale; MyScreen = native (physical);
     GUI/world scaling derived from logical = native/pixel_size
  -> world + in-game GUI render at logical res into the native surface as NxN blocks
  -> GL/CPU present 1:1
  (frontend/menu/movies: game_screen_active=false -> pixel_size=1 -> unchanged)
```

## Error handling / edge cases
- `render_scale` absent/invalid → clamp to `1`.
- Resolution/mode switch in-game → the setup re-runs; `pixel_size` re-derived.
- Failsafe / minimal video modes → treat as non-game (`pixel_size=1`).
- Frontend, land-view, movies → `pixel_size=1` (normal).

## Testing / validation (with the hard bail-out)
1. **Builds clean; `render_scale=1` byte-identical** — the critical regression guard (all derived values unchanged; provable by inspection).
2. **Headless boot** at `render_scale=2` (`-level N`): reaches menu + loads a level, no crash, world generates geometry (`geometry_bytes > 0`).
3. **Validation point #1 (make-or-break, user playtest):** in a level at `render_scale=2` — world renders, **HUD laid out correctly, mouse clicks land**. This is the one real unknown (in-game HUD/mouse at `pixel_size=2`).
   - Clean → essentially done.
   - Close but off → fix (iterations 2–3).
   - **A mess beyond ~3 rounds → ship gated-off or shelve. No marathon.** (User's explicit rule.)
4. Rough frame-time delta `render_scale=1` vs `2` to confirm the CPU win is real.

## Non-goals (v1)
Non-integer scales; GL sharp-bilinear upscaling of a smaller surface; launcher UI; heavy `pixel_size=3` polish if `2` suffices; the world-only-separate-parameter approach (B).
