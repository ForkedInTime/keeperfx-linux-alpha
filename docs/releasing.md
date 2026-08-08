# Cutting a release

Two channels share one line of history. Which one a build lands in is decided
entirely by **the tag and the prerelease flag** — nothing else in the pipeline
infers it.

| Channel | Tag | GitHub release | Reaches |
|---|---|---|---|
| stable | `v1.4.0.5423` | **not** prerelease | AppImage · full.7z · AUR · Flatpak |
| alpha  | `v1.4.0.5397-alpha` | **prerelease** | AppImage · full.7z |

## Steps

1. Commit the changelog entry and any version change.
2. Tag it. A stable tag carries no suffix; an alpha tag ends in `-alpha`.
3. Create the GitHub release for that tag — **and tick prerelease for an alpha.**
4. CI does the rest: `build-appimage.yml` builds and attaches the AppImage and
   `full.7z`; `publish-aur.yml` updates the AUR recipe. The Flatpak is monthly
   and self-updates, so it is not part of cutting a release.

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

## Ordering trap

`publish-aur.yml` pins the release's `full.7z` by hash, but that asset is
attached by the AppImage build, which runs *after* the release is published. On
a fresh release the archive is legitimately not there yet, so the workflow logs
a notice and skips rather than failing. Re-run it once the AppImage build has
uploaded.
