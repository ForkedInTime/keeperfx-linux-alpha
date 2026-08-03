# AUR packaging

Source for the `keeperfx-tux` AUR package. This directory is the
canonical copy; the AUR repository is a mirror of these three files plus a
generated `.SRCINFO`.

The package is **versioned against release tags**, not a `-git` package: it
builds whatever `_tag` points at, so `yay -Syu` shows an update like any other
package instead of requiring `--devel`. The cost is one bump per release, which
`.github/workflows/publish-aur.yml` does automatically.

## Two packages

| Package | Source | Size | Contents |
|---|---|---|---|
| `keeperfx-tux` | this repo at `_tag` | 16 MB | engine compiled from source, text config, wrapper, desktop entry, icons |
| `keeperfx-tux-data` (`data/`) | the release's `full.7z` | 412 MB | `data` `sound` `ldata` `campgns` `levels` `lang` `fxdata` `creatrs` `mods` `music` |

Both carry the same `_tag`/`pkgver` and are bumped together, deliberately: the data
package holds the config the engine reads, and a version mismatch between them is
precisely the staleness that once shipped a frozen config against a newer engine.
Since 1.4.0.5273 the release archive carries freshly-overlaid config — verified for
1.4.0.5366, where all 65 `fxdata`/`creatrs` files are byte-identical to the repo at
that tag — so the data package's copies are authoritative and the wrapper prefers
them.

The data package deliberately drops the archive's `keeperfx` binary, the Qt
launcher, the bundled `7z.so`, `keeperfx.cfg` and `version.txt`: those are either
the engine package's job or user state.

**Redistribution note.** The archive contains no files from the original Dungeon
Keeper except `data/main.pal` and `data/mapfadeg.dat`, which do appear in
`docs/files_required_from_original_dk.txt`. They arrive via the KeeperFX data tree
that upstream distributes, and the AUR hosts nothing itself — it points users at
the same release asset this project already publishes — so this package adds no
exposure that the AppImage and `full.7z` do not already carry. The other 14 listed
files are absent, and the wrapper names them individually when they are missing.

## What the engine package does and does not ship

Ships: the engine built from source against system libraries, the generated
UTF-8 fonts, and the text/config data that tracks the engine version
(`fxdata`, `creatrs`, `mods`, `campgns`, `levels`, `lang`) — enough to run
against a data tree the user already has.

Does not ship: the bulk game data, which is what `keeperfx-tux-data` is for, nor
the 14 files listed in `docs/files_required_from_original_dk.txt`, which come
from the user's own copy of Dungeon Keeper and are in no package at all.

## Why there is a wrapper instead of a plain binary

The engine derives every path it touches from the directory of `argv[0]` —
see `process_command_line()` in `src/main.cpp`, which takes `argv[0]`, strips
the last component, and stores the result as `keeper_runtime_directory`. Save
games (`FGrp_Save`), screenshots, `keeperfx.cfg` and `keeperfx.log` all resolve
against that directory, so a read-only `/usr` prefix cannot serve as the
runtime directory.

`/usr/bin/keeperfx-tux` therefore assembles a per-user game directory at
`$XDG_DATA_HOME/keeperfx-alpha` (override with `KEEPERFX_HOME`) and execs the
engine through a path inside it. Because the engine reads `argv[0]` rather than
`/proc/self/exe`, the symlink is not resolved and the engine roots itself in the
writable directory.

The game directory stays `keeperfx-alpha` rather than following the package
rename: that is the path `refresh-alpha.sh` and the Qt launcher already use, and
changing it would strand existing installs and stop the launcher from finding
data it had fetched.

Layout the wrapper maintains, resolving each tree against the data package first
and the engine package second:

| Kind | Directories | Behaviour |
|---|---|---|
| Read-only | `campgns` `levels` `lang` `fxdata` `creatrs` `ldata` | Symlinked whole, refreshed each launch so a pacman upgrade takes effect |
| Merged | `data` `sound` | Real directories; packaged files linked in **individually** so the user's own Dungeon Keeper files sit beside them. A real file already in place always wins |
| User-owned | `mods` `music` | Seeded once with `cp -rn`, never clobbered |
| Writable state | `save` `scrshots` | Created if absent |

If the user replaces a symlinked directory with a real one, the wrapper leaves
it alone — it only ever replaces its own symlinks.

## Maintenance

**Per release: nothing.** Publishing a GitHub release triggers
`.github/workflows/publish-aur.yml`, which rewrites `_tag`/`pkgver`, regenerates
`.SRCINFO`, commits the bump back here, and pushes to the AUR. It needs one
repository secret, `AUR_SSH_PRIVATE_KEY`, whose public half is registered on the
AUR account owning the package. Run it with **Actions > Publish to AUR > Run
workflow** and `dry_run: true` to validate without pushing.

**When you change the wrapper or desktop file**, their checksums are pinned, so
refresh them:

```bash
updpkgsums
makepkg --printsrcinfo > .SRCINFO
```

**First publish** (one-off, before the workflow can take over — the AUR creates
a package repo on first push):

```bash
git clone ssh://aur@aur.archlinux.org/keeperfx-tux.git aur-pkg
cp PKGBUILD .SRCINFO keeperfx-tux.sh keeperfx-tux.desktop aur-pkg/
cd aur-pkg && git add -A && git commit -m "Initial import" && git push
```

Building locally to test a change:

```bash
makepkg -f                       # build only
KEEPERFX_HOME=~/kfx-aur-test makepkg -si   # build, install, then run keeperfx-alpha
```

Use `KEEPERFX_HOME` when testing on a machine that already has a
`refresh-alpha.sh` install: the wrapper deliberately refuses to overwrite real
directories, so it would silently keep using the existing game directory instead
of the packaged data.

## Deliberate choices a reviewer may query

- **`prepare()` reaches the network.** `linux.mk` fetches centijson, astronomy,
  enet6 and libcurl rather than vendoring them. Doing it in `prepare()` at least
  keeps `build()` offline-shaped. Vendoring them as additional `source=()`
  entries would be cleaner and is the obvious follow-up.
- **`!strip`.** This is an alpha; usable backtraces in bug reports are worth
  more than the saved megabytes.
- **`!lto`.** The makefile manages its own optimisation flags, and LTO has not
  been validated for this codebase. Letting makepkg inject it risks a silent
  miscompile of the kind that already forced a `-march` revert.

## Verification status

Done: wrapper logic (game-directory assembly, idempotency, preservation of
user-edited mods and of a real directory replacing a symlink, `argv[0]`
rooting), and a full `makepkg` build from the release tag — 16 MB package,
correct version string, all seven icon sizes, no bundled `.so` files, and `ldd`
reporting no missing libraries.

Not done: a **clean-chroot** build (`extra-x86_64-build`, needs `devtools`),
which is what would catch a dependency that happens to be installed on the
build machine but missing from `depends`. Worth running once before the first
AUR publish. Also untested: launching the packaged game end-to-end, which needs
a populated `data/` + `sound/`.
