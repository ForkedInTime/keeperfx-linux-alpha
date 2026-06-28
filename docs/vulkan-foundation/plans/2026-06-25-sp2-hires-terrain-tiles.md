# Hi-res Truecolor Terrain Tiles (Proof Pack) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render ~8–12 of the highest-impact terrain tiles as hi-res truecolor art (authored offline) through the SP1 GPU world renderer, gated by `KFX_HIRES=1`, with the engine/lighting/gameplay unchanged — ending in an in-game visual the user confirms.

**Architecture:** Two decoupled halves meeting at a `hires/` PNG folder. (A) An **offline Python toolset** (`tools/hires/`) that decompresses the real RNC tiles, upscales them seamlessly (Real-ESRGAN on a 3×3 wrap), adds detail (SDXL-Turbo img2img with circular-padding convolutions = native tiling), color-matches back toward the DK palette (Reinhard LAB transfer), and writes RGBA8 PNGs keyed by block id. (B) A **runtime override path** in `bflib_render_glworld.{c,h}`: a `GL_TEXTURE_2D_ARRAY` (RGBA8, `GL_REPEAT`+`GL_LINEAR`) holding the hi-res tiles, a `block_id→layer` lookup texture, a per-shade brightness LUT derived from `fade_tables`, and a terrain-fragment-shader branch that samples the array and multiplies by shade — falling back to the existing paletted path for non-overridden tiles.

**Tech Stack:** Python 3 (PIL, numpy, pytest) + `realesrgan-ncnn-vulkan` + diffusers/`sdxl-turbo` (venv at `scratchpad/assetproof/.venv`) for the offline half; C + OpenGL 3.3 core (libepoxy) + `spng` for the runtime half. Build: CMake + Ninja.

## Global Constraints

- Native Linux x86-64 (LP64). All new GL/renderer code fenced `#ifndef _WIN32`; the Windows build keeps the paletted path.
- `-Werror` clean; BOTH `keeperfx` and `keeperfx_hvlog` must build. Build: `cmake --build bin-linux -- -k 0`.
- Renderer edits confined to `src/src/bflib_render_glworld.{c,h}` plus minimal `KFX_HIRES` lifecycle wiring in `src/src/bflib_video.c` if needed. No change to engine geometry/projection/sort/sim, the SP1 command-list/occlusion, or the GUI/sprite paths.
- Opt-in via `KFX_HIRES=1`. With it unset (default), behavior is **byte-for-byte identical to SP1** (the override lookup is empty → existing `index → fade_tables[shade] → palette` path runs unchanged).
- `/opt/keeperfx-personal` game data is **read-only** — never modified. Generated art lives in a separate opt-in `hires/` folder. Stock `tmap*.dat` are never edited.
- Offline tools live under `tools/hires/` and are NOT compiled into the game.
- Watch SP1's LP64 hazards (no `long`-width casts of fixed-width data; no `min(a-b,b-a)` abs-tricks). Use `int32_t`/`uint32_t`/`size_t`.
- The texture-store abstraction stays intact so a future full tileset reuses this override path with no renderer-architecture change.
- LANGUAGE: offline scripts are plain Python 3; renderer is C (the module is C, compiled as C).

**Verified facts the tasks rely on (from the codebase + the pre-implementation proof):**
- Tiles: 32×32 8-bit paletted, 1024 bytes each. `tmap{a,b}NNN.dat` are **RNC ProPack** (`RNC\x01`) compressed; decompress with the engine's `rnc_unpack` (`src/src/bflib_dernc.c`). `TMAPA000.DAT` → 557056 bytes = 544 tiles. World palette: `DATA/PALETTE.DAT` (RNC → 768 bytes, 6-bit VGA, scale `*255//63`). Data dir: `/opt/keeperfx-personal/DATA/`.
- Material block ids on TMAPA000 (from the proof contact sheet): floor/path ≈ 48–55, brick wall ≈ 8–23, earth/rock ≈ 36–43, gold seam ≈ 80–95, lava ≈ 56–71. Final ids confirmed in Task 1 Step 2 via the contact sheet + `config/fxdata/cubes.cfg`.
- SP1 terrain fragment shader (`src/src/bflib_render_glworld.c`, the `glw_fs` string near line 493): vertex attrs `aPos/aUV/aShade/aLayer`; varyings `vUV` (0..1 over tile), `vShade`, `flat vLayer`; uniforms `uTiles` (usampler2D R8UI atlas), `uFade` (usampler2D 256×64), `uPal` (sampler2D 256×1), `uAtlasCols`, `uTileDim` (=32). Resolve: `tid=int(vLayer+0.5)` → cell `(tid%uAtlasCols, tid/uAtlasCols)` → `palIdx` → `faded=uFade[palIdx,shade]` → `rgb=uPal[faded]`. Uniform locations cached in `gw.u_tiles/u_fade/u_pal/u_atlascols/u_tiledim`; program built in the function near line 560 (`glGetUniformLocation`).
- Texstore upload: `glworld_texstore_sync()` (near line 700) builds `gw.tile_tex` from `block_ptrs[]` into `glw_atlasbuf` when `gw.texstore_dirty`, then uploads `uFade` from `pixmap.fade_tables` and `uPal` from `lbPaletteColors` every call. `gw.atlas_cols`, `gw.atlas_w`, `gw.atlas_h` define the cell grid (cells are `GLW_TILE_DIM`=32). `lbPaletteColors` is `256*3` bytes 0..255; `pixmap.fade_tables` is `64*256` bytes (row = shade 0..63, col = palette index → remapped index).
- PNG decode: use `spng` (already linked; see `src/src/custom_sprites.c` `read_png_icon` for the API pattern: `spng_ctx_new` → `spng_set_png_buffer` → `spng_decode_image(..., SPNG_FMT_RGBA8, ...)`).

---

### Task 1: Offline hi-res tile pipeline → the proof-pack PNGs

Builds the `tools/hires/` Python toolset and runs it to produce the actual proof-pack art. The deliverable is both the reusable scripts (with pure-Python unit tests) and the generated `hires/` folder of RGBA8 tiles. This is independently reviewable: "does it produce seamless, on-material, palette-matched hi-res tiles from the real game data?"

**Files:**
- Create: `tools/hires/dernc_main.c` (standalone RNC decompressor around the engine's `bflib_dernc.c`)
- Create: `tools/hires/build_dernc.sh` (compiles `dernc`)
- Create: `tools/hires/pipeline.py` (extract → upscale → detail → palette-match → pack; the pure stages are importable)
- Create: `tools/hires/run_pipeline.sh` (end-to-end driver: builds dernc, decompresses, runs pipeline for the manifest)
- Create: `tools/hires/manifest.txt` (the proof-pack tile list: `<blockid> <material>`)
- Create: `tools/hires/tests/test_pipeline.py` (pytest for the pure stages)
- Create: `tools/hires/README.md` (how to run; dependency notes)
- Output (generated, git-ignored): `hires/tmap_000_<blockid>.png` (RGBA8 256×256) + `hires/manifest.txt`

**Interfaces:**
- Produces (Python, importable from `pipeline.py`):
  - `decode_tile(raw: bytes, palette: np.ndarray, block_id: int) -> np.ndarray` — returns 32×32×3 uint8 RGB for one tile (`raw` = decompressed tmap, `palette` = 256×3 uint8).
  - `load_palette(raw768: bytes) -> np.ndarray` — 256×3 uint8 (auto-scales 6-bit→8-bit).
  - `seamless_upscale(tile_rgb: np.ndarray, esrgan_bin: str, model: str, workdir: str) -> np.ndarray` — 128×128×3 uint8, seamless (3×3 wrap → ESRGAN ×4 → center crop).
  - `palette_match(detailed_rgb: np.ndarray, source_rgb: np.ndarray, strength: float) -> np.ndarray` — Reinhard LAB transfer of `detailed` toward `source`; `strength` in 0..1 (default 0.7); preserves shape.
  - `tiling_seam_score(tile_rgb: np.ndarray) -> float` — mean absolute edge-wrap discontinuity (0 = perfectly seamless); used by tests.
- The SDXL detail stage (`detail_tile`) is driven by `run_pipeline.sh` (needs the GPU venv) and is NOT unit-tested (non-deterministic, GPU); its output feeds `palette_match`.
- Produces (on disk) the `hires/` folder consumed by Task 2's loader: filenames `tmap_<variation:03d>_<blockid:03d>.png`, RGBA8, 256×256, alpha=255.

- [ ] **Step 1: Create the standalone RNC decompressor**

`tools/hires/dernc_main.c` (the engine's `bflib_dernc.c` provides `rnc_unpack`; it needs a few stubs because it lives in a TU with file-IO helpers we don't call):

```c
// tools/hires/dernc_main.c — decompress an RNC ProPack file using the engine's algorithm.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bflib_dernc.h"
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s in.dat out.raw\n", argv[0]); return 2; }
    FILE *f = fopen(argv[1], "rb"); if (!f) { perror("in"); return 2; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *buf = (unsigned char *)malloc(n);
    if (fread(buf, 1, n, f) != (size_t)n) { return 2; } fclose(f);
    unsigned int usize = ((unsigned)buf[4] << 24) | ((unsigned)buf[5] << 16)
                       | ((unsigned)buf[6] << 8) | buf[7];      // RNC header: unpacked size (BE) at off 4
    unsigned char *out = (unsigned char *)malloc(usize + 64);
    long r = rnc_unpack(buf, out, 0);
    if (r < 0) { fprintf(stderr, "rnc_unpack err %ld (hdr usize=%u)\n", r, usize); return 1; }
    FILE *o = fopen(argv[2], "wb"); fwrite(out, 1, r, o); fclose(o);
    fprintf(stderr, "ok: %ld bytes -> %s\n", r, argv[2]);
    return 0;
}
```

`tools/hires/build_dernc.sh`:

```bash
#!/usr/bin/env bash
# Build the standalone RNC decompressor from the engine's bflib_dernc.c.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/../../src/src"
VERDIR="$HERE/../../src/bin-linux"   # generated ver_defs.h lives here after a normal build
cat > "$HERE/dernc_stubs.c" <<'C'
#include <stdlib.h>
unsigned long lword(unsigned char *p){ return (unsigned long)p[0] | ((unsigned long)p[1]<<8); }
long get_gameturn(void){ return 0; }
void LbErrorLog(const char *fmt,...){ (void)fmt; }
int  LbFileOpen(const char *a,int b){ (void)a;(void)b; return -1; }
int  LbFileClose(int h){ (void)h; return -1; }
int  LbFileRead(int h,void*b,unsigned long n){ (void)h;(void)b;(void)n; return -1; }
int  LbFileWrite(int h,const void*b,unsigned long n){ (void)h;(void)b;(void)n; return -1; }
long LbFileLengthHandle(int h){ (void)h; return -1; }
C
gcc -O2 -I"$SRC" -I"$VERDIR" -o "$HERE/dernc" \
    "$HERE/dernc_main.c" "$SRC/bflib_dernc.c" "$HERE/dernc_stubs.c"
echo "built $HERE/dernc"
```

- [ ] **Step 2: Confirm the proof-pack block ids**

Build dernc, decompress `TMAPA000.DAT`, render a labeled contact sheet, and pick the final ids by cross-referencing `config/fxdata/cubes.cfg` (slab cube faces → block indices). Write the chosen ids to `tools/hires/manifest.txt`:

```bash
bash tools/hires/build_dernc.sh
mkdir -p /tmp/hires_work
tools/hires/dernc /opt/keeperfx-personal/DATA/TMAPA000.DAT /tmp/hires_work/tmapa000.raw
tools/hires/dernc /opt/keeperfx-personal/DATA/PALETTE.DAT  /tmp/hires_work/palette.raw
python3 tools/hires/pipeline.py contact /tmp/hires_work/tmapa000.raw /tmp/hires_work/palette.raw /tmp/hires_work/contact.png
```

`tools/hires/manifest.txt` (start from the proof ids; adjust after viewing the contact sheet — keep 8–12 static, non-animated tiles spanning materials):

```
# blockid  material
50  floor
16  wall
40  earth
89  gold
12  wall2
52  floor2
36  earth2
93  gold2
```

(Avoid ids in the animated range; if a chosen id falls in 56–71 (lava) note it as a STATIC-only override.)

- [ ] **Step 3: Write the pure pipeline stages + their failing tests**

`tools/hires/tests/test_pipeline.py`:

```python
import numpy as np
from tools.hires import pipeline as P

def test_load_palette_scales_6bit():
    raw = bytes([0,0,0, 63,0,0, 0,63,0] + [0]*(768-9))   # 6-bit max -> should scale to 255
    pal = P.load_palette(raw)
    assert pal.shape == (256, 3)
    assert pal[1,0] == 255 and pal[2,1] == 255          # 63 -> 255
    assert pal[0].tolist() == [0,0,0]

def test_decode_tile_shape_and_color():
    pal = np.zeros((256,3), np.uint8); pal[5] = (10,20,30)
    raw = bytes([5]*1024) * 1                              # one tile, all index 5
    t = P.decode_tile(raw, pal, 0)
    assert t.shape == (32,32,3)
    assert t[0,0].tolist() == [10,20,30]

def test_palette_match_preserves_shape_and_moves_mean():
    rng = np.random.default_rng(0)
    src = (rng.random((32,32,3))*60).astype(np.uint8)      # dark source
    det = (rng.random((64,64,3))*255).astype(np.uint8)     # bright detailed
    out = P.palette_match(det, src, strength=1.0)
    assert out.shape == det.shape and out.dtype == np.uint8
    assert out.mean() < det.mean()                         # pulled toward the darker source

def test_tiling_seam_score_zero_for_constant():
    flat = np.full((64,64,3), 100, np.uint8)
    assert P.tiling_seam_score(flat) < 1.0
```

`tools/hires/pipeline.py` (pure stages + a `contact` CLI; the SDXL stage is separate, Step 5):

```python
import sys, os, subprocess, tempfile
import numpy as np
from PIL import Image

def load_palette(raw768: bytes) -> np.ndarray:
    d = np.frombuffer(raw768[:768], dtype=np.uint8).reshape(256, 3).astype(np.uint16)
    if d.max() <= 63:                       # 6-bit VGA
        d = (d * 255 + 31) // 63
    return d.astype(np.uint8)

def decode_tile(raw: bytes, palette: np.ndarray, block_id: int) -> np.ndarray:
    off = block_id * 1024
    idx = np.frombuffer(raw[off:off+1024], dtype=np.uint8).reshape(32, 32)
    return palette[idx]

def seamless_upscale(tile_rgb, esrgan_bin, model, workdir):
    os.makedirs(workdir, exist_ok=True)
    tiled = np.tile(tile_rgb, (3, 3, 1))                      # 96x96 wrap context
    src = os.path.join(workdir, "in.png"); out = os.path.join(workdir, "out.png")
    Image.fromarray(tiled, "RGB").save(src)
    subprocess.run([esrgan_bin, "-i", src, "-o", out, "-n", model, "-s", "4"],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    up = np.asarray(Image.open(out).convert("RGB"))           # 384x384
    return up[128:256, 128:256].copy()                        # center tile = seamless 4x

def _rgb_to_lab(a):
    return np.asarray(Image.fromarray(a, "RGB").convert("LAB"), dtype=np.float32)
def _lab_to_rgb(a):
    return np.asarray(Image.fromarray(np.clip(a,0,255).astype(np.uint8), "LAB").convert("RGB"))

def palette_match(detailed_rgb, source_rgb, strength=0.7):
    d = _rgb_to_lab(detailed_rgb); s = _rgb_to_lab(source_rgb)
    out = d.copy()
    for c in range(3):
        dm, ds = d[...,c].mean(), d[...,c].std() + 1e-3
        sm, ss = s[...,c].mean(), s[...,c].std() + 1e-3
        shifted = (d[...,c] - dm) * (ss / ds) + sm           # full Reinhard transfer
        out[...,c] = d[...,c]*(1-strength) + shifted*strength
    return _lab_to_rgb(out)

def tiling_seam_score(tile_rgb):
    a = tile_rgb.astype(np.float32)
    horiz = np.abs(a[:,0,:] - a[:,-1,:]).mean()
    vert  = np.abs(a[0,:,:] - a[-1,:,:]).mean()
    return float((horiz + vert) / 2.0)

def _contact(tmap_raw_path, pal_raw_path, out_path):
    pal = load_palette(open(pal_raw_path,"rb").read())
    raw = open(tmap_raw_path,"rb").read(); n = len(raw)//1024
    from PIL import ImageDraw
    cols=16; cell=48; pad=12; rows=(n+cols-1)//cols
    W=cols*(cell+2)+2; H=rows*(cell+pad+2)+2
    sheet=Image.new("RGB",(W,H),(18,18,22)); dr=ImageDraw.Draw(sheet)
    for i in range(n):
        r=i//cols; c=i%cols
        im=Image.fromarray(decode_tile(raw,pal,i),"RGB").resize((cell,cell),Image.NEAREST)
        x=2+c*(cell+2); y=2+r*(cell+pad+2); sheet.paste(im,(x,y))
        dr.text((x+1,y+cell+1),str(i),fill=(210,210,170))
    sheet.save(out_path); print("wrote",out_path,sheet.size)

if __name__ == "__main__":
    if sys.argv[1] == "contact":
        _contact(sys.argv[2], sys.argv[3], sys.argv[4])
```

- [ ] **Step 4: Run the unit tests (red → green)**

```bash
cd /mnt/Storage/Projects/keeperfx/src && python3 -m pytest tools/hires/tests/test_pipeline.py -v
```
Expected: all 4 pass. (If `pytest` import path fails, run with `PYTHONPATH=/mnt/Storage/Projects/keeperfx/src`.)

- [ ] **Step 5: Write the SDXL detail stage + the end-to-end driver**

`tools/hires/detail.py` (GPU; reuses the cached venv):

```python
# Adds material detail with native tiling (circular-padding convs). Run via the GPU venv.
import sys, torch, torch.nn as nn
from diffusers import AutoPipelineForImage2Image
from PIL import Image
import numpy as np

PROMPTS = {
 "floor":"weathered dungeon stone floor, cobblestone, cracked pitted rock, intricate detail, dark fantasy, sharp",
 "wall": "dungeon brick wall, deep mortar joints, weathered carved stone blocks, masonry detail, dark fantasy",
 "earth":"dark packed soil, cave dirt floor, small rocks pebbles roots, organic detail, dark fantasy",
 "gold": "glittering raw gold ore veins in dark rock, treasure nuggets, sparkling detail, dark fantasy mine",
 "lava": "molten lava, glowing orange magma cracks, bright embers, dark volcanic crust, dark fantasy"}

def base_material(m):                    # floor2 -> floor, etc.
    return ''.join(ch for ch in m if not ch.isdigit())

def main(in_png, material, out_png, strength=0.55, steps=7, seed=3):
    pipe = AutoPipelineForImage2Image.from_pretrained(
        "stabilityai/sdxl-turbo", torch_dtype=torch.float16, variant="fp16").to("cuda")
    pipe.set_progress_bar_config(disable=True)
    for mod in list(pipe.unet.modules()) + list(pipe.vae.modules()):
        if isinstance(mod, nn.Conv2d):
            mod.padding_mode = 'circular'
    base = Image.open(in_png).convert("RGB").resize((512,512), Image.LANCZOS)
    g = torch.Generator("cuda").manual_seed(seed)
    prompt = PROMPTS[base_material(material)]
    img = pipe(prompt=prompt, image=base, strength=strength,
               num_inference_steps=steps, guidance_scale=0.0, generator=g).images[0]
    img.save(out_png)

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3])
```

`tools/hires/run_pipeline.sh` (end-to-end; produces the `hires/` pack):

```bash
#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DATA="${KFX_ASSETS:-/opt/keeperfx-personal}/DATA"
# SDXL venv (with diffusers + cached sdxl-turbo). Point KFX_HIRES_VENV at the
# proof venv (scratchpad/assetproof/.venv) or a repo-local tools/hires/.venv.
VENV="${KFX_HIRES_VENV:-$ROOT/tools/hires/.venv}"
ESRGAN="${KFX_ESRGAN:-/usr/bin/realesrgan-ncnn-vulkan}"
MODEL="${KFX_ESRGAN_MODEL:-realesrgan-x4plus}"
VAR="${KFX_HIRES_VAR:-000}"
WORK="$(mktemp -d)"; OUT="$ROOT/hires"; mkdir -p "$OUT"
bash "$ROOT/tools/hires/build_dernc.sh"
"$ROOT/tools/hires/dernc" "$DATA/TMAPA${VAR}.DAT" "$WORK/tmap.raw"
"$ROOT/tools/hires/dernc" "$DATA/PALETTE.DAT"     "$WORK/pal.raw"
cp "$ROOT/tools/hires/manifest.txt" "$OUT/manifest.txt"
grep -vE '^\s*#|^\s*$' "$ROOT/tools/hires/manifest.txt" | while read -r BID MAT _; do
  python3 - "$WORK/tmap.raw" "$WORK/pal.raw" "$BID" "$WORK/src_${BID}.png" <<'PY'
import sys; from tools.hires import pipeline as P; from PIL import Image
raw=open(sys.argv[1],'rb').read(); pal=P.load_palette(open(sys.argv[2],'rb').read())
Image.fromarray(P.decode_tile(raw,pal,int(sys.argv[3])),"RGB").save(sys.argv[4])
PY
  python3 - "$WORK/src_${BID}.png" "$ESRGAN" "$MODEL" "$WORK/up_${BID}.png" "$WORK" <<'PY'
import sys, numpy as np; from tools.hires import pipeline as P; from PIL import Image
up=P.seamless_upscale(np.asarray(Image.open(sys.argv[1]).convert("RGB")),sys.argv[2],sys.argv[3],sys.argv[5])
Image.fromarray(up,"RGB").save(sys.argv[4])
PY
  "$VENV/bin/python" "$ROOT/tools/hires/detail.py" "$WORK/up_${BID}.png" "$MAT" "$WORK/det_${BID}.png"
  python3 - "$WORK/det_${BID}.png" "$WORK/src_${BID}.png" "$OUT/tmap_${VAR}_$(printf %03d "$BID").png" <<'PY'
import sys, numpy as np; from tools.hires import pipeline as P; from PIL import Image
det=np.asarray(Image.open(sys.argv[1]).convert("RGB"))
src=np.asarray(Image.open(sys.argv[2]).convert("RGB"))
matched=P.palette_match(det, src, strength=0.7)
out=Image.fromarray(matched,"RGB").resize((256,256),Image.LANCZOS).convert("RGBA")
out.save(sys.argv[3]); print("wrote",sys.argv[3])
PY
done
echo "pack written to $OUT"
```

Run it (set `PYTHONPATH` so `tools.hires` imports):

```bash
cd /mnt/Storage/Projects/keeperfx/src && PYTHONPATH=. bash tools/hires/run_pipeline.sh
```
Expected: `hires/tmap_000_*.png` (RGBA 256×256) for each manifest id, plus `hires/manifest.txt`.

- [ ] **Step 6: Verify the pack (seamless + on-material) and write the README**

```bash
cd /mnt/Storage/Projects/keeperfx/src && PYTHONPATH=. python3 - <<'PY'
import glob, numpy as np; from tools.hires import pipeline as P; from PIL import Image
for f in sorted(glob.glob("hires/tmap_000_*.png")):
    a=np.asarray(Image.open(f).convert("RGB"))
    print(f, "seam=%.2f"%P.tiling_seam_score(a), "size", a.shape[:2])
    assert a.shape[:2]==(256,256)
PY
```
Expected: every tile 256×256 with a low seam score (well under, say, 12.0 — circular padding keeps it seamless). Write `tools/hires/README.md` documenting the env vars (`KFX_HIRES_VENV`, `KFX_ESRGAN`, `KFX_ESRGAN_MODEL`, `KFX_HIRES_VAR`) and the run command.

- [ ] **Step 7: Commit**

```bash
cd /mnt/Storage/Projects/keeperfx/src
echo "/hires/" >> .gitignore
git add tools/hires .gitignore
git commit -m "feat(hires): offline ESRGAN+SDXL seamless tile pipeline + palette-match (tools/hires)"
```
(The generated `hires/` art is git-ignored — it's a build artifact, regenerated from stock data.)

---

### Task 2: Renderer override store + loader (textures only, no shader change yet)

Adds the GPU override store and the loader that fills it from `hires/`, gated by `KFX_HIRES`. No shader/visual change yet — this task is verifiable as "textures created and populated, lookup correct, no GL errors, `KFX_HIRES` off changes nothing."

**Files:**
- Modify: `src/src/bflib_render_glworld.h` (override interface + `gl_hires_active` flag)
- Modify: `src/src/bflib_render_glworld.c` (override store, PNG loader, lookup build, lifecycle)
- Modify: `src/src/bflib_video.c` (set `KFX_HIRES` opt-in alongside `KFX_GLWORLD`, mirroring the existing env handling)

**Interfaces:**
- Consumes: SP1's `gw` state (`gw.inited`, `gw.atlas_cols`, `gw.atlas_w`, `gw.atlas_h`), `glworld_texstore_sync()`, the GL context. The `hires/tmap_<var>_<id>.png` files from Task 1.
- Produces (in `bflib_render_glworld.h`, inside the existing `#ifndef _WIN32` fence except the flag):
  - `extern TbBool gl_hires_active;` (declared outside the fence, like `gl_world_active`)
  - `void glworld_hires_load(const char *dir);` — scan `dir` for `tmap_*_*.png`, decode, upload into the override array, build the `block_id→layer` lookup. No-op (clears the store) if `dir` is NULL/empty or `!gl_hires_active`.
  - `void glworld_hires_shutdown(void);` — delete the override array, lookup texture, free CPU buffers.
  - `GLuint glworld_hires_array(void);` / `GLuint glworld_hires_lookup_tex(void);` / `int glworld_hires_count(void);` — accessors for Task 3's shader binding.
- Internals (file-static in `bflib_render_glworld.c`): `gw_hires` struct holding `GLuint ov_array` (GL_TEXTURE_2D_ARRAY RGBA8, `GLW_HIRES_DIM`=256, N layers), `GLuint ov_lookup` (R16UI, `gw.atlas_cols × gw.atlas_rows`, value = layer or `GLW_HIRES_NONE`=0xFFFF), `int count`.

- [ ] **Step 1: Add the header interface**

In `src/src/bflib_render_glworld.h`, near the `gl_world_active` declaration, add (outside the fence):

```c
/** True when the hi-res terrain override path is active (Linux only, KFX_HIRES=1,
 *  glworld inited, and at least one override tile loaded). Set by glworld_hires_load. */
extern TbBool gl_hires_active;
```

Inside the `#ifndef _WIN32` block, add:

```c
/** Reserved lookup value meaning "no hi-res override for this tile". */
#define GLW_HIRES_NONE 0xFFFFu
/** Edge length (px) of one hi-res override tile (square). */
#define GLW_HIRES_DIM  256

/** Load hi-res override tiles from `dir` (files tmap_<var>_<blockid>.png, RGBA8).
 *  Builds a GL_TEXTURE_2D_ARRAY (GLW_HIRES_DIM^2 x N, RGBA8, GL_REPEAT, GL_LINEAR,
 *  mipmapped) and a block_id->layer lookup texture (R16UI, sized like the tile atlas;
 *  GLW_HIRES_NONE elsewhere). Sets gl_hires_active true iff >=1 tile loads. Safe to
 *  call when KFX_HIRES is off (clears the store, leaves gl_hires_active false). */
void glworld_hires_load(const char *dir);
/** Delete override GL objects and free CPU buffers. */
void glworld_hires_shutdown(void);
/** Override array texture (0 if none). */
GLuint glworld_hires_array(void);
/** Override block_id->layer lookup texture (0 if none). */
GLuint glworld_hires_lookup_tex(void);
/** Number of override tiles loaded. */
int glworld_hires_count(void);
```

- [ ] **Step 2: Implement the PNG loader + override store (the code)**

In `src/src/bflib_render_glworld.c` (inside the `#ifndef _WIN32` region), add the state, a small spng-based RGBA loader (pattern from `custom_sprites.c:read_png_icon`), and the load/shutdown. Define `TbBool gl_hires_active = false;` at file scope (outside the fence, near `gl_world_active`).

```c
#include <spng.h>
#include <dirent.h>

static struct {
    GLuint ov_array;      /* GL_TEXTURE_2D_ARRAY RGBA8, GLW_HIRES_DIM^2 x count */
    GLuint ov_lookup;     /* R16UI, atlas_cols x atlas_rows: block_id -> layer */
    int    count;
} gw_hires;

/* Decode one PNG file to a freshly malloc'd RGBA8 buffer of GLW_HIRES_DIM^2.
 * Returns NULL on any failure or a size mismatch. Caller frees. */
static unsigned char *glw_hires_decode_png(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return NULL; }
    unsigned char *file = (unsigned char *)malloc((size_t)n);
    if (fread(file, 1, (size_t)n, f) != (size_t)n) { free(file); fclose(f); return NULL; }
    fclose(f);
    spng_ctx *ctx = spng_ctx_new(0);
    spng_set_png_buffer(ctx, file, (size_t)n);
    struct spng_ihdr ihdr;
    if (spng_get_ihdr(ctx, &ihdr) != 0 ||
        ihdr.width != GLW_HIRES_DIM || ihdr.height != GLW_HIRES_DIM) {
        spng_ctx_free(ctx); free(file); return NULL;
    }
    size_t out_size = 0;
    spng_decoded_image_size(ctx, SPNG_FMT_RGBA8, &out_size);
    unsigned char *rgba = (unsigned char *)malloc(out_size);
    int r = spng_decode_image(ctx, rgba, out_size, SPNG_FMT_RGBA8, 0);
    spng_ctx_free(ctx); free(file);
    if (r != 0) { free(rgba); return NULL; }
    return rgba;
}

/* Parse "tmap_<var>_<blockid>.png" -> blockid, or -1 if it doesn't match. */
static int glw_hires_parse_id(const char *name)
{
    int var, bid;
    if (sscanf(name, "tmap_%d_%d.png", &var, &bid) == 2) return bid;
    return -1;
}

void glworld_hires_shutdown(void)
{
    if (gw_hires.ov_array)  { glDeleteTextures(1, &gw_hires.ov_array);  gw_hires.ov_array = 0; }
    if (gw_hires.ov_lookup) { glDeleteTextures(1, &gw_hires.ov_lookup); gw_hires.ov_lookup = 0; }
    gw_hires.count = 0;
    gl_hires_active = false;
}

void glworld_hires_load(const char *dir)
{
    glworld_hires_shutdown();
    if (!gw.inited || dir == NULL || dir[0] == '\0') return;

    /* Collect matching files (block_id + path), capped to a sane proof-pack size. */
    enum { GLW_HIRES_MAX = 64 };
    int  ids[GLW_HIRES_MAX]; char paths[GLW_HIRES_MAX][512]; int found = 0;
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) != NULL && found < GLW_HIRES_MAX) {
        int bid = glw_hires_parse_id(de->d_name);
        if (bid < 0) continue;
        ids[found] = bid;
        snprintf(paths[found], sizeof(paths[found]), "%s/%s", dir, de->d_name);
        found++;
    }
    closedir(d);
    if (found == 0) return;

    /* Clamp to runtime GL limits (array layers + texture size). */
    GLint maxlayers = 0, maxsz = 0;
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &maxlayers);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxsz);
    if (maxsz < GLW_HIRES_DIM) return;               /* can't hold a tile; stay paletted */
    if (found > maxlayers) found = maxlayers;

    /* Build the array texture. */
    glGenTextures(1, &gw_hires.ov_array);
    glBindTexture(GL_TEXTURE_2D_ARRAY, gw_hires.ov_array);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, GLW_HIRES_DIM, GLW_HIRES_DIM, found,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    /* CPU lookup, sized like the tile atlas; default = NONE. */
    size_t lutw = (size_t)gw.atlas_cols, luth = (size_t)gw.atlas_rows;
    uint16_t *lut = (uint16_t *)malloc(lutw * luth * sizeof(uint16_t));
    for (size_t i = 0; i < lutw * luth; i++) lut[i] = GLW_HIRES_NONE;

    int layer = 0;
    for (int i = 0; i < found; i++) {
        unsigned char *rgba = glw_hires_decode_png(paths[i]);
        if (!rgba) continue;                          /* skip bad file -> paletted fallback */
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, GLW_HIRES_DIM, GLW_HIRES_DIM, 1,
                        GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        free(rgba);
        int bid = ids[i];
        if (bid >= 0 && (size_t)bid < lutw * luth) lut[bid] = (uint16_t)layer;
        layer++;
    }
    gw_hires.count = layer;

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (layer > 0) glGenerateMipmap(GL_TEXTURE_2D_ARRAY);

    /* Upload the lookup as R16UI. */
    glGenTextures(1, &gw_hires.ov_lookup);
    glBindTexture(GL_TEXTURE_2D, gw_hires.ov_lookup);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, (GLsizei)lutw, (GLsizei)luth, 0,
                 GL_RED_INTEGER, GL_UNSIGNED_SHORT, lut);
    free(lut);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    gl_hires_active = (layer > 0);
    (void)glworld_check_error("glworld_hires_load");
}

GLuint glworld_hires_array(void)      { return gw_hires.ov_array; }
GLuint glworld_hires_lookup_tex(void) { return gw_hires.ov_lookup; }
int    glworld_hires_count(void)      { return gw_hires.count; }
```

NOTE: if `gw.atlas_rows` does not already exist, derive it where needed as `(GLW_TILE_COUNT + gw.atlas_cols - 1) / gw.atlas_cols` and add an `int atlas_rows;` to the `gw` struct set in `glw_choose_atlas_layout()`. Confirm the exact field name during implementation and use it consistently.

- [ ] **Step 3: Wire the lifecycle (load on init, shutdown on teardown)**

In `glworld_shutdown()` add `glworld_hires_shutdown();`. After a successful `glworld_init(...)`, call `glworld_hires_load(<hires dir>)` once the env opt-in is set. In `src/src/bflib_video.c`, alongside the existing `KFX_GLWORLD` handling, read `KFX_HIRES`:

```c
const char *kfx_hires = getenv("KFX_HIRES");
TbBool hires_enabled = (kfx_hires != NULL) && (kfx_hires[0] == '1');
```

When `hires_enabled` and the GL world initialised, resolve the hi-res dir (default `"hires"` relative to the run dir, overridable via `KFX_HIRES_DIR`) and call `glworld_hires_load(dir)`. When not enabled, do not call it (store stays empty, `gl_hires_active` false).

- [ ] **Step 4: Build (both targets) and headless smoke test**

```bash
cd /mnt/Storage/Projects/keeperfx/src && cmake --build bin-linux -- -k 0 2>&1 | grep -E 'error:|warning:'; echo "exit ${PIPESTATUS[0]}"
```
Expected: empty grep, both targets build. Then run with the pack present and check the log + no GL errors:

```bash
KFX_GLWORLD=1 KFX_HIRES=1 KFX_POSTFX=0 KFX_WINDOWED=1 ./run-linux.sh -nointro -level 1 -pause_at_gameturn 120 2>&1 | tail -5
grep -iE "glworld_hires|GL error|shader|link" ~/.local/share/keeperfx-linux/run/keeperfx.log | tail
```
Expected: reaches "Started level 1", `glworld_hires_load` loads N tiles, 0 GL errors. With `KFX_HIRES=0`: store empty, `gl_hires_active` false, no behavior change.

- [ ] **Step 5: Commit**

```bash
git add src/src/bflib_render_glworld.c src/src/bflib_render_glworld.h src/src/bflib_video.c
git commit -m "feat(hires): GPU override store + PNG loader + KFX_HIRES gating (no shader change yet)"
```

---

### Task 3: Shader override branch + shade-as-multiply

Extends the terrain fragment shader to sample the override array (when a tile has an override) and multiply by a per-shade brightness derived from `fade_tables`, so engine lighting still plays across hi-res tiles. This is where the hi-res tiles first become visible.

**Files:**
- Modify: `src/src/bflib_render_glworld.c` (terrain fragment shader string; new uniforms + locations; shade-scale LUT computed in `glworld_texstore_sync()`; bind override array + lookup + shade LUT before the terrain draw)

**Interfaces:**
- Consumes: Task 2's `glworld_hires_array()`, `glworld_hires_lookup_tex()`, `gl_hires_active`; SP1's terrain program + `gw.u_*` uniform locations; `pixmap.fade_tables`, `lbPaletteColors`.
- Produces: a visually-overriding terrain pass — overridden tiles sample the RGBA8 array; all others use the unchanged paletted chain.

- [ ] **Step 1: Compute the shade-scale LUT (C side)**

Add a 64-entry RGB brightness LUT (per shade level), uploaded as a `64×1 RGB32F` texture `gw.shade_tex`, computed once per palette change in `glworld_texstore_sync()` (gate on the same dirty/palette-change condition used for `uPal`). For each shade `s` (0..63): `scale[s] = mean_over_idx( luminance(palette[fade_tables[s*256+idx]]) ) / mean_over_idx( luminance(palette[idx]) )`, clamped to [0,2]. Use a single luminance (0.299/0.587/0.114) for all three channels (grayscale multiplier) for v1.

```c
/* near the other gw GLuint fields */ GLuint shade_tex; GLint u_shade, u_ov_array, u_ov_lookup, u_ov_cols;
```

In `glworld_texstore_sync()`, where `uPal` is uploaded, also compute + upload the shade LUT:

```c
{
    float shade_lut[64*3];
    double base = 0.0;
    for (int i = 0; i < 256; i++) {
        const unsigned char *c = &lbPaletteColors[i*3];
        base += 0.299*c[0] + 0.587*c[1] + 0.114*c[2];
    }
    base = (base / 256.0) + 1e-3;
    for (int s = 0; s < 64; s++) {
        double acc = 0.0;
        for (int i = 0; i < 256; i++) {
            unsigned char fi = pixmap.fade_tables[s*256 + i];
            const unsigned char *c = &lbPaletteColors[fi*3];
            acc += 0.299*c[0] + 0.587*c[1] + 0.114*c[2];
        }
        float sc = (float)((acc / 256.0) / base);
        if (sc < 0.0f) sc = 0.0f; if (sc > 2.0f) sc = 2.0f;
        shade_lut[s*3+0] = shade_lut[s*3+1] = shade_lut[s*3+2] = sc;
    }
    if (gw.shade_tex == 0) {
        glGenTextures(1, &gw.shade_tex);
        glBindTexture(GL_TEXTURE_2D, gw.shade_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, 64, 1, 0, GL_RGB, GL_FLOAT, shade_lut);
    } else {
        glBindTexture(GL_TEXTURE_2D, gw.shade_tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 64, 1, GL_RGB, GL_FLOAT, shade_lut);
    }
}
```
Delete `gw.shade_tex` in `glworld_shutdown()`.

- [ ] **Step 2: Extend the terrain fragment shader**

In the terrain fragment shader string (the `glw_fs` near line 493), add the override uniforms and branch. After computing `int tid`, `int shade`, and the existing `palIdx`, replace the final color resolve so an override short-circuits:

```glsl
// add to the uniform block:
uniform sampler2DArray uOvArray;   // RGBA8 hi-res override tiles
uniform usampler2D     uOvLookup;  // R16UI block_id -> layer (0xFFFF = none)
uniform sampler2D      uShade;     // 64x1 RGB: per-shade brightness multiplier
uniform int            uOvCols;    // = uAtlasCols (lookup texture width)

// ... after: int tid = int(vLayer + 0.5);  int shade = clamp(int(floor(vShade+0.5)),0,63);
uint ovl = texelFetch(uOvLookup, ivec2(tid % uOvCols, tid / uOvCols), 0).r;
if (ovl != 0xFFFFu) {
    vec3 hi = texture(uOvArray, vec3(vUV, float(ovl))).rgb;   // REPEAT+LINEAR+mip
    vec3 sc = texelFetch(uShade, ivec2(shade, 0), 0).rgb;
    oColor = vec4(hi * sc, 1.0);
} else {
    // existing paletted path (unchanged):
    uint faded = texelFetch(uFade, ivec2(int(palIdx), shade), 0).r;
    vec3 rgb   = texelFetch(uPal, ivec2(int(faded), 0), 0).rgb;
    oColor = vec4(rgb, 1.0);
}
```
Keep the existing tile-UV wrap math for `palIdx` (needed in the else branch). Cache new uniform locations after link:

```c
gw.u_ov_array  = glGetUniformLocation(prog, "uOvArray");
gw.u_ov_lookup = glGetUniformLocation(prog, "uOvLookup");
gw.u_shade     = glGetUniformLocation(prog, "uShade");
gw.u_ov_cols   = glGetUniformLocation(prog, "uOvCols");
```

- [ ] **Step 3: Bind the override textures before the terrain draw**

Where the terrain program is used and `uTiles/uFade/uPal` are bound (the terrain draw in `glworld_end_frame`/flush), bind the override units and set `uOvCols = gw.atlas_cols`. Use texture units beyond the ones SP1 already uses (pick free units, e.g. 4/5/6):

```c
glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D_ARRAY,
        gl_hires_active ? glworld_hires_array() : 0);
glUniform1i(gw.u_ov_array, 4);
glActiveTexture(GL_TEXTURE5); glBindTexture(GL_TEXTURE_2D,
        gl_hires_active ? glworld_hires_lookup_tex() : 0);
glUniform1i(gw.u_ov_lookup, 5);
glActiveTexture(GL_TEXTURE6); glBindTexture(GL_TEXTURE_2D, gw.shade_tex);
glUniform1i(gw.u_shade, 6);
glUniform1i(gw.u_ov_cols, gw.atlas_cols);
```
When `gl_hires_active` is false, the lookup texture is 0 → `texelFetch` returns 0 → but 0 is a valid layer/`!=0xFFFF`. To keep the OFF path identical, guard the branch with `gl_hires_active`: bind a valid lookup only when active, and when inactive ensure the shader's `ovl` reads as NONE. Simplest robust approach: when `!gl_hires_active`, the lookup texture handle is the one built with all-`0xFFFF` is absent, so instead set `uOvCols` to a sentinel `0` and make the shader treat `uOvCols<=0` as "no overrides" (skip straight to the paletted path). Implement that guard in the shader:

```glsl
uint ovl = (uOvCols > 0) ? texelFetch(uOvLookup, ivec2(tid % uOvCols, tid / uOvCols), 0).r : 0xFFFFu;
```
and set `glUniform1i(gw.u_ov_cols, gl_hires_active ? gw.atlas_cols : 0);`. This makes the OFF path provably take the original branch.

- [ ] **Step 4: Build + headless parity/override check**

```bash
cd /mnt/Storage/Projects/keeperfx/src && cmake --build bin-linux -- -k 0 2>&1 | grep -E 'error:|warning:'; echo "exit ${PIPESTATUS[0]}"
```
Expected: empty, both targets. Then capture FBO dumps both ways:

```bash
KFX_GLWORLD=1 KFX_HIRES=0 KFX_POSTFX=0 KFX_WINDOWED=1 KFX_GLWORLD_DUMP=1 ./run-linux.sh -nointro -level 1 -pause_at_gameturn 200
cp /tmp/glworld.ppm /tmp/hires_off.ppm
KFX_GLWORLD=1 KFX_HIRES=1 KFX_POSTFX=0 KFX_WINDOWED=1 KFX_GLWORLD_DUMP=1 ./run-linux.sh -nointro -level 1 -pause_at_gameturn 200
cp /tmp/glworld.ppm /tmp/hires_on.ppm
python3 - <<'PY'
import numpy as np
from PIL import Image
off=np.asarray(Image.open("/tmp/hires_off.ppm")); on=np.asarray(Image.open("/tmp/hires_on.ppm"))
print("off vs on changed pixels:", int((np.abs(off.astype(int)-on.astype(int)).sum(2)>16).sum()),
      "of", off.shape[0]*off.shape[1])
PY
```
Expected: `KFX_HIRES=0` reaches gameplay, 0 GL errors; `KFX_HIRES=1` differs (overridden tiles changed) but the scene is coherent (no black/garbage); non-overridden regions identical. If the override tiles don't appear, debug the lookup/`uOvCols` wiring before proceeding.

- [ ] **Step 5: Commit**

```bash
git add src/src/bflib_render_glworld.c
git commit -m "feat(hires): terrain shader override branch + shade-as-multiply (hi-res tiles visible)"
```

---

### Task 4: In-game visual checkpoint, fallback hardening, and the user verdict

Produces the artifact the user signs off on — a real in-game render comparing `KFX_HIRES=1` vs `=0` — and hardens the fallbacks. **The user confirms the look visually here; the spec/build passing is not the acceptance.**

**Files:** Modify any of the above for fixes found; no new modules expected.

- [ ] **Step 1: Fallback + safety sweep**

Verify each path degrades safely, capturing evidence:
- No `hires/` dir / `KFX_HIRES_DIR` points nowhere → `glworld_hires_load` no-ops, `gl_hires_active` false, game runs paletted.
- A corrupt/ø-size PNG in `hires/` → that tile skipped (paletted), others load, no crash.
- Force `GL_MAX_ARRAY_TEXTURE_LAYERS`/size clamp (temporarily lower the cap in code) → loads up to the cap, logs, runs; restore after.
- `KFX_HIRES=0` → byte-identical scene-FBO vs SP1 (re-confirm Task 3's off/on diff shows 0 changed pixels when off vs the SP1 baseline).

- [ ] **Step 2: Build both targets -Werror clean**

```bash
cd /mnt/Storage/Projects/keeperfx/src && cmake --build bin-linux -- -k 0 2>&1 | grep -E 'error:|warning:'; echo "exit ${PIPESTATUS[0]}"
```
Expected: empty; both `keeperfx` and `keeperfx_hvlog`.

- [ ] **Step 3: Render the in-game visual comparison (the checkpoint)**

Capture the same scene at the same gameturn both ways and build a side-by-side the user can judge. Prefer a real window screenshot if a display is available; otherwise use the engine's existing in-game screenshot path or the `glworld_debug_dump` FBO at full resolution.

```bash
for H in 0 1; do
  KFX_GLWORLD=1 KFX_HIRES=$H KFX_POSTFX=1 KFX_WINDOWED=1 KFX_GLWORLD_DUMP=1 \
    ./run-linux.sh -nointro -level 1 -pause_at_gameturn 300
  cp /tmp/glworld.ppm /tmp/ingame_hires_$H.ppm
done
python3 - <<'PY'
from PIL import Image
a=Image.open("/tmp/ingame_hires_0.ppm").convert("RGB")
b=Image.open("/tmp/ingame_hires_1.ppm").convert("RGB")
w,h=a.size; cmp=Image.new("RGB",(w*2+20,h),(16,16,20))
cmp.paste(a,(0,0)); cmp.paste(b,(w+20,0))
cmp.save("/tmp/ingame_hires_compare.png"); print("wrote /tmp/ingame_hires_compare.png",cmp.size)
PY
```
The controller sends `/tmp/ingame_hires_compare.png` (and the two raw frames) to the user and asks for the go/no-go. **Do not mark Sub-project 2 complete until the user confirms the look.** Capture the chosen scene(s) at the user's resolution if requested.

- [ ] **Step 4: Update the spec status + commit**

After the user's visual confirmation, update the status line in `docs/superpowers/specs/2026-06-25-sp2-hires-terrain-tiles-design.md` to note the proof pack is rendered and confirmed, then:

```bash
git add -A
git commit -m "feat(hires): in-game proof-pack render + fallback hardening; Sub-project 2 proof complete"
```

---

## Notes for implementers

- **The texture store is the seam.** Don't special-case the proof tiles in the renderer beyond the override lookup — scaling to the full tileset must be "drop more PNGs in `hires/`", no code change.
- **Parity-off is sacred.** Any path where `KFX_HIRES` is unset must be byte-identical to SP1. The `uOvCols<=0` shader guard is the mechanism — keep it.
- **Lighting via shade-multiply** is an approximation of the paletted fade chain; if a hi-res tile looks mis-lit vs its paletted neighbour across shade levels, revisit the `shade_lut` derivation (per-channel instead of luminance) before adding complexity.
- **Don't touch** engine geometry/sim, the SP1 command-list/occlusion, or the sprite/GUI paths.
- The exact `gw` field names (`atlas_rows`, uniform-location members) are read during implementation; match the existing SP1 naming.
