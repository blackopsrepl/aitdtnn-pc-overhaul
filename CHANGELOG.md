# Changelog

All notable changes to this project will be documented in this file. See [commit-and-tag-version](https://github.com/absolute-version/commit-and-tag-version) for commit guidelines.

## [0.5.1](https://github.com/blackopsrepl/aitdtnn-pc-overhaul/compare/v0.5.0...v0.5.1) (2026-08-22)

### Changed

* split every oversized first-party implementation, installer, and test file into responsibility-based modules below 300 physical lines
* add a build-enforced source-layout limit so monolithic files cannot silently return
* split the asset builder, installer transaction manager, and Inno wizard into independently navigable modules

### Documentation

* add reader-oriented source comments and a plain-language codebase guide for contributors unfamiliar with C++ or Windows hooking
* split the technical report into linked audio, renderer, and rumble/installer/validation documents
* refresh the root, audio, renderer, rumble, and shared-loader documentation surfaces

### Validation

* prove the rearranged native sources reproduce all four 0.5.0 runtime DLLs byte-for-byte
* validate direct and packaged extraction of 69 scene banks and 161 sequences
* pass the full install, upgrade-refusal, rollback, uninstall, and modified-tree preservation lifecycle

## [0.5.0](https://github.com/blackopsrepl/aitdtnn-pc-overhaul/compare/v0.4.0...v0.5.0) (2026-08-22)

### Bug Fixes

* **audio:** preserve shared-bank music transitions 13f2f16

### Documentation

* clarify the game-side DSEQ scheduling and Dreamcast Manatee/AICA rendering boundary
* document the complete 69-container collision audit

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
