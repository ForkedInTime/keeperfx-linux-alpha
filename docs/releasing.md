# Cutting a release

Two channels share one line of history. Which one a build lands in is decided
entirely by **the tag and the prerelease flag** — nothing else in the pipeline
infers it.

| Channel | Tag | GitHub release | Reaches |
|---|---|---|---|
| stable | `v1.4.0.5423` | **not** prerelease | AppImage · full.7z · AUR · Flatpak |
| alpha  | `v1.4.0.5397-alpha` | **prerelease** | AppImage · full.7z |

## Steps

0. **If the launcher changed, push its `alpha` branch first.** See below — this is
   the step that is easy to forget and produces a release that looks fine.
1. Commit the changelog entry and any version change.
2. Tag it. A stable tag carries no suffix; an alpha tag ends in `-alpha`.
3. Create the GitHub release for that tag — **and tick prerelease for an alpha.**
4. CI does the rest: `build-appimage.yml` builds and attaches the AppImage and
   `full.7z`; `publish-aur.yml` updates the AUR recipe. The Flatpak is monthly
   and self-updates, so it is not part of cutting a release.

## The launcher ships from its own branch

The launcher is a **separate repository**
(`ForkedInTime/keeperfx-launcher-qt-linux`), and `build-appimage.yml` clones it at
a fixed branch:

```yaml
LAUNCHER_BRANCH: alpha
git clone --branch "$LAUNCHER_BRANCH" --depth 1 "$LAUNCHER_REPO" launcher
```

Day-to-day launcher work happens on its **`master`**. So a release only carries
launcher changes if `alpha` has been fast-forwarded to `master` first:

```bash
git -C <launcher> push origin master:alpha
```

**Why this deserves its own step:** nothing fails if you skip it. The engine
release builds, every workflow goes green, the assets attach, and the changelog
describes launcher features that are not in the binary. It looks like the
features are broken rather than absent. This happened while cutting
`v1.4.0.5465-alpha`: `alpha` was six commits behind and the whole launcher
release would have shipped without a single one of its changes.

Two ways to confirm it worked, in order of effort:

- before tagging: `git -C <launcher> rev-list --count origin/alpha..origin/master`
  should be `0`;
- after the build: the AppImage run log should show the new sources compiling,
  e.g. `Building CXX object ... logviewerdialog.cpp.o` for the release that added
  the log viewer.

## Why the prerelease flag matters

GitHub defines `releases/latest` as *the newest release that is not a
prerelease*. Several things resolve through it:

- the README's download button, which promises the newest **stable** build
- `build-appimage.yml` and `build-flatpak.yml`, which fetch the previous
  `full.7z` from `releases/latest` to use as the payload base

Alpha releases were not flagged, so `latest` simply meant *most recently
published*. Because alphas ship far more often than stables, the next alpha
would have taken over that link — handing stable users an alpha, and building
the next stable's payload on top of an alpha's. All 27 existing alpha releases
were flagged retroactively on 2026-08-08; keep new ones flagged.

## Why the AUR only gets stable

`publish-aur.yml` skips any tag ending in `-alpha` or `-prototype`.

pacman upgrades on version alone. Before this filter existed, an alpha tag was
published to the AUR with its suffix stripped — `v1.4.0.5397-alpha` became
pkgver `1.4.0.5397` — so `yay -Syu` moved Arch users onto alpha builds silently,
indistinguishably from stable, at alpha cadence. The launcher has a
release-channel setting so a user can opt into alphas; a distro package has no
equivalent, so the filtering happens at publish time.

If Arch users should ever be able to follow the alpha line, that wants a
separate `keeperfx-tux-alpha` package they opt into by name — the usual Arch
convention — not a silent version bump.

## The stable-release trap: `latest` becomes the new release immediately

`build-appimage.yml` builds each release's payload by overlaying onto the previous
release's `full.7z`. It used to fetch that base from `releases/latest` — which is
safe for an alpha, because a prerelease never becomes `latest`, and wrong for a
stable, because publishing a stable makes it `latest` *the instant it is
published*, before its own assets exist. The build then tried to download its own
payload, got a 404, and `wget` exited 8 with no useful message.

This bit the first stable patch, `v1.4.0.5425`. The workflow now asks the API for
the newest **non-prerelease** release that actually carries
`keeperfx-linux-alpha-x86_64-full.7z`, skipping the tag being built, so it can
never resolve to itself — and still never bases a stable payload on an alpha's,
which is the rule the prerelease flag exists to enforce.

If you ever need the old manual escape: flag the new stable as prerelease, re-run
the failed build so `latest` falls back to the previous stable, then clear the
flag.

## The AUR bookkeeping step used to gate the AUR push

`publish-aur.yml` commits the version/checksum bump back to this repo and *then*
pushes to the AUR. That commit pushed straight at the default branch, so any
commit landing there in between made it fail — and because the AUR push is gated
on that step, the package silently never went out. The push is now rebased and
non-fatal; a failure warns and the AUR push still runs.

## Ordering trap

`publish-aur.yml` pins the release's `full.7z` by hash, but that asset is
attached by the AppImage build, which runs *after* the release is published. On
a fresh release the archive is legitimately not there yet, so the workflow logs
a notice and skips rather than failing. Re-run it once the AppImage build has
uploaded.

## The save-format guard

A saved game is a raw memory dump of `struct Game`, and the engine loads one
only if its length equals `sizeof(struct Game)` for the running build. That
number is therefore the save format version, and a field added anywhere inside
any struct it reaches invalidates every save in existence — with no warning, and
nothing in the diff that looks like a save-format change. It happened on
2026-08-19 (`+ GameTurn last_turn_damaged` in `struct Thing`, ×12288 things) and
cost five campaigns in progress.

`build-linux-alpha.yml` now measures the size of the build it is about to ship
and compares it with `packaging/ci/save-format-baseline`; a mismatch fails the
release build. The same check runs on the weekly upstream-sync PR, which is the
direction the break came from. Run it yourself before tagging:

```bash
packaging/ci/check-save-format.sh
```

If it fails, read `packaging/ci/save-format-baseline` — the choice is to keep the
new state out of `struct Game`, or to accept the break by editing that file in
the same commit. Accepting it is a user-visible decision: it belongs in the
changelog, and the release body gets an automatic "saves from earlier versions
cannot be loaded" warning, written by the release build off that file's history.

## Build system

This fork builds with `linux.mk`, not upstream's CMake. The reasons — their
pkg-config module names for SDL3 are wrong so it silently vendors SDL from
source, its Linux dependency set omits `libswscale` and `libepoxy`, and its
`WIN32` source filter is Windows-first — are set out in the README under
"Why this fork builds with `linux.mk`".

The practical consequence when syncing upstream: **new source files must be
added to `linux.mk` by hand.** Upstream updates their `Makefile` and CMake and
has no reason to touch ours, so a merge that compiles for them can fail to link
for us. #5099 added four files under `src/kfx/platform/` and also needed `-Isrc`,
which upstream's Makefile has always carried.
