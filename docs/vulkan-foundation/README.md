# Vulkan / 3D foundation (frozen reference — NOT built)

This folder is the groundwork for the **separate** long-term goal of taking KeeperFX to **3D /
Vulkan**. It is **reference only** — none of it is compiled into the alpha, which is deliberately
present-only (smooth GPU output of the team's existing 2D renderer).

The KeeperFX team's vision is to keep and perfect the original's 2D art. This 3D direction is a
different vision, pursued separately and on its own timeline.

## What's here

- **`world-renderer/`** — `bflib_render_glworld.{c,h}`: a truecolor GPU **world renderer** built
  earlier on the v1.3.2 fork. It intercepts the engine's depth-sorted bucket dispatch and renders
  terrain + sprite billboards on the GPU (2.5D, parity with software). It is frozen here because:
  - the **architecture/seam** (where and how to divert the engine's draw dispatch to the GPU, and
    composite the 2D GUI on top) is the reusable, hard-won part for *any* GPU renderer, including a
    future Vulkan/3D one;
  - the **2.5D drawing itself** would be replaced by real 3D meshes, so it's not carried in the
    live alpha (it would only cost recurring rebase friction for code a 3D renderer discards).
- **`specs/`** — the design docs:
  - `truecolor-gpu-world-renderer-design.md` — the renderer/seam design.
  - `sp2-hires-terrain-tiles-design.md`, `sp3-hires-object-sprites-design.md` — the (shelved)
    hi-res asset experiments. Kept for the lessons, not to revive (AI-upscaling muddied DK's crisp
    art — the real path to fidelity is 3D models, not upscaled 2D).

## When the 3D work actually starts

Start fresh against the **then-current upstream master** (the live alpha's source), using the seam
design here as the reference for where to hook the engine. Don't try to graft this exact code onto
a future master — re-establish the interception point against whatever master looks like then.
