# AUR packaging

Source for the `keeperfx-linux-alpha-git` AUR package. This directory is the
canonical copy; the AUR repository is a mirror of these three files plus a
generated `.SRCINFO`.

## What the package does and does not ship

Ships: the engine built from source against system libraries, the generated
UTF-8 fonts, and the text/config data that tracks the engine version
(`fxdata`, `creatrs`, `mods`, `campgns`, `levels`, `lang`).

Does not ship: `data/` and `sound/`. Those are Dungeon Keeper's own files and
are not redistributable, so the user supplies them — via the Qt launcher, the
`full.7z` release archive, or a symlink into an existing installation. The
wrapper detects their absence and prints all three routes.

## Why there is a wrapper instead of a plain binary

The engine derives every path it touches from the directory of `argv[0]` —
see `process_command_line()` in `src/main.cpp`, which takes `argv[0]`, strips
the last component, and stores the result as `keeper_runtime_directory`. Save
games (`FGrp_Save`), screenshots, `keeperfx.cfg` and `keeperfx.log` all resolve
against that directory, so a read-only `/usr` prefix cannot serve as the
runtime directory.

`/usr/bin/keeperfx-alpha` therefore assembles a per-user game directory at
`$XDG_DATA_HOME/keeperfx-alpha` (override with `KEEPERFX_HOME`) and execs the
engine through a path inside it. Because the engine reads `argv[0]` rather than
`/proc/self/exe`, the symlink is not resolved and the engine roots itself in the
writable directory.

Layout the wrapper maintains:

| Kind | Directories | Behaviour |
|---|---|---|
| Read-only | `fxdata` `creatrs` `campgns` `levels` `lang` | Symlinked to `/usr/share`, refreshed each launch so pacman upgrades take effect |
| User-owned | `mods` `music` | Seeded once with `cp -rn`, never clobbered |
| User-supplied | `data` `sound` | Required; the wrapper refuses to launch without them |
| Writable state | `save` `scrshots` | Created if absent |

If the user replaces a symlinked directory with a real one, the wrapper leaves
it alone — it only ever replaces its own symlinks.

## Maintenance

Bump the vendored wrapper or desktop file, then refresh checksums and metadata:

```bash
updpkgsums
makepkg --printsrcinfo > .SRCINFO
```

`pkgver()` derives the version the same way the project does: the base from
`version.mk`, the build number from the commit count. The `pkgver` field in the
PKGBUILD is only a placeholder — makepkg overwrites it at build time.

Publishing to the AUR:

```bash
git clone ssh://aur@aur.archlinux.org/keeperfx-linux-alpha-git.git aur-pkg
cp PKGBUILD keeperfx-alpha.sh keeperfx-alpha.desktop .SRCINFO aur-pkg/
cd aur-pkg && git commit -am "update to 1.4.0.x" && git push
```

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

## Not yet verified

The wrapper logic is tested (assembly, idempotency, user-data preservation,
`argv[0]` rooting). A full `makepkg` run — clean-chroot build, install, launch —
has **not** been done from this directory yet. Do that before publishing.
