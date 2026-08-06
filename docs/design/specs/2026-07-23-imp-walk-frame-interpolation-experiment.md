# Experiment Scope: Smoother Creature Animation via AI Frame Interpolation (imp walk)

**Date:** 2026-07-23
**Status:** **Closed — tried, and the answer was no.** Kept as the record of *why*, so the idea does not get proposed a third time.
**Type:** Asset-pipeline experiment (NOT an engine change).
**Goal:** Test whether AI-interpolating a creature's walk animation from N → 2N frames reads as *visibly smoother* while still looking like Dungeon Keeper — the "more drawings in the flip-book" idea.

## Outcome

Path A was run on `TMAGE_WALK` with RIFE at 2×. The interpolated cycle is smoother, but only *slightly* — nowhere near enough to justify building an asset pipeline around it, let alone the legacy-sprite extractor that Path B needs before the imp could even be attempted. The in-betweens on chunky low-res sprites are soft rather than wrong; the eye reads them as blur, not as extra animation.

**Do not invest further in the 2D interpolation route.** The honest path to fluid creature animation is 3D models with real skeletal animation, plus the GPU renderer — a different project, not an extension of this one.

Everything below is the original scope as written before the test, preserved unchanged.

## How creature animation works here (verified)

- Each animation is a `KeeperSprite` with a `FramesCount` (number of frames) and `Rotable` (view angles).
- Creatures are drawn from **~5 rendered angles**, mirror-flipped to 8 directions (`creature_graphics.c:317`).
- **Movement** (position/rotation between the 20 TPS sim) is *already* interpolated → creatures glide. **Animation frames are NOT interpolated** — the walk cycle is a fixed handful of frames. *That* is the target.
- Walk = `CGI_Ambulate` (anim index 1); playback rate = `WALKINGANIMSPEED` per-creature config (`config_crtrmodel.c:188,1615`).

## The clean injection path (asset-only, no engine change)

The fork already ships creatures as **custom PNG sprites** defined by a `sprites.json` inside the fxdata zips. Format (from `creatures.zip`):
```json
{ "name": "ORC_EAT", "rotatable": false,
  "td_offset_x": 30, "td_offset_y": 40,
  "td": [ [ {"file": "orc/eatchicken_td/r1frame01.png"}, {"file": "...r1frame02.png"}, ... ] ],
  "fp": [ [ ... ] ] }
```
- `td` = isometric view, `fp` = first-person; each is an array of **angle** arrays; each angle is an array of **frame** `{"file"}` entries. `FramesCount` = number of frames listed. `r1/r3/r5` naming = the mirror-folded angles.
- **So "double the frames" = insert AI-interpolated PNGs between each pair, list them, and halve the frame duration via `WALKINGANIMSPEED`** (2× frames at 2× speed = same real-time cadence, smoother).
- The custom-sprite system loads these by `name`; a creature's `[sprites]` config block binds an animation to a sprite (`parse_creaturemodel_sprites_blocks`, `config_crtrmodel.c:2181`), so we can **override** the imp's walk by name.

## The imp-specific catch

`creatures.zip` contains the fork's **new** PNG animations (orc-eat, avatar-torture) — **the base imp is still legacy `.dat`** (baked `KeeperSprite`, not PNG). There is **no legacy→PNG extractor** in `tools/` (only fxfontmaker + png2ico). So the imp path has a prerequisite step the already-PNG creatures don't.

## Two ways to run the experiment

**Path A — Prove the pipeline first on an already-PNG animation (recommended).**
Use `ORC_EAT` (or avatar torture) — already extractable PNGs in `creatures.zip`. Zero extraction. This isolates the *real* unknown — **does AI frame-interpolation on chunky DK sprites look good, or mushy?** — for near-zero cost. If it looks good, *then* invest in the imp's extraction.

**Path B — Imp directly.** Requires first extracting the imp's legacy walk frames to PNG: either write a small `KeeperSprite`→PNG dumper (the format is known: `KeeperSprite` struct + the sprite data files), or source community-extracted DK1 imp sprites. Then interpolate → repack → override.

## The pipeline (either path)

1. **Extract** the animation's frames as PNGs, per angle (`r1/r3/r5`). [Path A: already PNG. Path B: needs the dumper.]
2. **Interpolate** each angle's frame sequence 2× with an AI frame-interpolator — **RIFE** (`rife-ncnn-vulkan`, easiest) or **FILM** (higher quality, large-motion). Loop the cycle so the last→first frame also gets an in-between.
3. **Post-process:** if the sprites are 8-bit indexed, re-quantize the interpolated output back to the game palette; trim/pad to the original frame bounds; preserve transparency (color-key).
4. **Repack:** add the new PNGs, extend the `sprites.json` frame lists, drop into a mod zip (or override `creatures.zip`).
5. **Playback:** set `WALKINGANIMSPEED` so the doubled frames play at the original duration.
6. **Observe:** in-game, side-by-side with the original — does it read smoother, and still like DK?

## Risks / unknowns (the real ones)

- **Interpolation quality on tiny low-res pixel art is the make-or-break.** RIFE/FILM are trained on HD natural video; on a 64px imp with big frame-to-frame motion (a fast walk cycle), in-betweens can be mushy or produce ghost limbs. **This is exactly why Path A exists — cheap answer first.**
- **Angle continuity:** each of the ~5 angles interpolated independently; no cross-angle consistency needed (they're separate), so this is fine.
- **Indexed color / palette:** interpolation in truecolor then re-quantize; small risk of palette drift.
- **Cyclic motion:** must interpolate the wrap-around (last→first) or the loop stutters.

## Recommendation & first step

Run **Path A** on `ORC_EAT` (or the imp's walk once extracted) as a **one-animation, in-game A/B** — the smallest thing that answers "does this look good." It's reversible (a mod zip), engine-untouched, and cheap. If the interpolation reads as smoother-and-still-DK, we know the whole approach is viable and worth scaling (imp extraction, more animations, more creatures). If it's mushy, we've spent an afternoon and learned the honest answer — and the *real* fluidity path is then the 3D-model route (sprite-derived 3D creatures + the GPU renderer).

**First concrete step to greenlight:** install a frame-interpolator (rife-ncnn-vulkan), extract one animation's PNG frames, 2× them, and eyeball the before/after frames *before* wiring any of it into the game.
