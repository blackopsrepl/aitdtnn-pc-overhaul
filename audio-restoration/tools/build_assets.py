#!/usr/bin/env python3
"""Public CLI for extracting runtime music data from an owned Dreamcast disc.

The implementation is split by concern under asset_builder/. Patch symbols are
re-exported for legacy recovery tests; the installer only calls main().
"""
from __future__ import annotations
import argparse
import sys
import zlib
from pathlib import Path
from asset_builder.assets import build_assets
from asset_builder.disc import open_disc
from asset_builder.executable_patch import *

def main() -> int:
    """Validate arguments, open the disc source and build one runtime tree."""
    parser = argparse.ArgumentParser(description="Build AITD:TNN Dreamcast audio runtime assets")
    parser.add_argument("dreamcast_image", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        with open_disc(args.dreamcast_image.resolve()) as disc:
            manifest = build_assets(disc, args.output.resolve(), str(args.dreamcast_image))
    except (OSError, ValueError, zlib.error) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(f"Scene banks: {manifest['scene_banks']}")
    print(f"Bank files: {manifest['bank_files']}")
    print(f"Program map entries: {manifest['program_map_entries']}")
    print(f"Sequences: {manifest['sequences']}")
    print(f"Output: {args.output.resolve()}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
