# KeeperFX Alpha (personal native-Linux build)

This is **upstream KeeperFX `master`** — the KeeperFX team's own native-Linux port (the
bleeding-edge "alpha" they maintain between releases) — **plus a small set of personal fixes**,
built native for Linux. It is for **personal use only**: it is *not* an upstream contribution and
is *not* for redistribution.

## Two visions, one clean update path

- **The KeeperFX team's vision:** preserve and perfect the original Dungeon Keeper's 2D art and
  feel. Their `master` is the canonical engine, and this project tracks it.
- **This fork's separate interest:** eventually take it to **3D / Vulkan**. That work is kept
  entirely separate — see [`docs/vulkan-foundation/`](docs/vulkan-foundation/). It is **not** part
  of the alpha build.

Every few months we pull the team's latest `master`, see what they improved, and lift our fixes
on top — their 2D improvements flow in, our personal fixes ride along, nothing is contributed back.

## What our fixes add (the one `alpha` commit on top of master)

- **OpenGL "present" layer** (`bflib_render_gl.*`) — smooth GPU output (8-bit framebuffer →
  GPU → screen), with a minimal world-renderer stub so it links present-only.
- **Ultrawide creature-possession crash fix** (`LensManager.cpp`) — master's lens buffer had no
  margin for the viewport offset; at 3440×1440 possessing a creature read past it (SIGSEGV).
- **Clean-exit fix** (`main.cpp`) — quitting raced SDL3's Wayland teardown thread (Arch's
  `sdl2-compat` runs SDL2-over-SDL3) and segfaulted; we hard-exit before the teardown.
- **`linux.mk` wiring** — libepoxy + the new sources, and `-Wno-error` for newer GCC.

## Build & install

Dependencies (Arch): `sdl2 sdl2_mixer sdl2_net sdl2_image ffmpeg openal luajit enet zlib minizip
libepoxy curl miniupnpc libnatpmp zstd` + `git make gcc pkgconf`. Then just:

```sh
./refresh-alpha.sh
```

It builds the engine, deploys the matching config/text data, and installs to
`~/.local/share/keeperfx-alpha/` (launcher: `keeperfx-alpha`).

To build by hand: `make -f linux.mk` fetches deps then compiles to `bin/keeperfx` (fetch the
curl-downloaded deps serially first if you use `-j` — see `refresh-alpha.sh`).

## Refreshing to the team's latest master (every ~6 months)

```sh
git fetch upstream
git log --oneline HEAD~1..upstream/master   # SEE exactly what the team improved
./refresh-alpha.sh                          # rebases our fixes onto it, builds, installs
```

`upstream` is `https://github.com/dkfans/keeperfx` (the team's repo). We only ever **fetch** from
it; nothing is pushed.

## Credits & licence

KeeperFX is the work of the [KeeperFX team and contributors](https://github.com/dkfans/keeperfx)
(dkfans). This project is their engine with personal fixes layered on. Licensed under the **GNU
GPL v2 (or later)**, same as upstream. Requires you own the original Dungeon Keeper (16 data files,
copied into the install at first setup).

*Dungeon Keeper is a trademark of Electronic Arts. Not affiliated with or endorsed by EA, Bullfrog,
or the KeeperFX team.*
