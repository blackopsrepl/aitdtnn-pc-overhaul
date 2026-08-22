# Changelog

All notable changes to this project will be documented in this file. See [commit-and-tag-version](https://github.com/absolute-version/commit-and-tag-version) for commit guidelines.

## Unreleased

### Documentation

* clarify the game-side DSEQ scheduling and Dreamcast Manatee/AICA rendering boundary
* distinguish an unresolved layered music slot from an entire gameplay scene
* document the complete 69-container collision audit and identify over-strict slot suppression

### Fixed

* replace heuristic music-container inference with loader-associated identity and full DSEQ validation
* resolve the formerly ambiguous title/menu `jardinv0` layer from its exact live load association
* preserve active cross-room sequences when a transition replaces their shared Dreamcast bank
* rebind unchanged active players after repeated shared-bank reloads

## [0.4.0](https://github.com/blackopsrepl/aitdtnn-pc-overhaul/compare/v0.3.0...v0.4.0) (2026-08-21)

### Features

* **audio:** stream Dreamcast AICA through Miles 8732fe9

### Bug Fixes

* refresh bundled installer 378f38f
## 0.2.0 — 2026-08-20

- User-facing documentation with game screenshots.
- Windows installer builds for GitHub Actions and Forgejo Actions.
- Verified payload preservation in the Windows build pipeline.
