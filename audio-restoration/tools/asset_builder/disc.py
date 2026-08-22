"""Disc-source adapters for directories, ISO, BIN/CUE and GDI images."""
from __future__ import annotations
import re
import shlex
import struct
from pathlib import Path
from .iso9660 import GdRomIso, Iso9660, TrackSpec

class DiscFiles:
    def read_file(self, path: str) -> bytes:
        raise NotImplementedError

    def list_files(self, path: str) -> list[str]:
        raise NotImplementedError

    def close(self) -> None:
        pass

    def __enter__(self) -> "DiscFiles":
        return self

    def __exit__(self, *_: object) -> None:
        self.close()


class DirectoryDisc(DiscFiles):
    def __init__(self, root: Path):
        candidates = (root, root / "data", root / "DATA")
        self.root = next((candidate for candidate in candidates if self._has_root_files(candidate)), root)
        if not self._has_root_files(self.root):
            raise ValueError(f"{root}: MANATEE.DRV and ALONE4.MLT were not found")

    @staticmethod
    def _resolve(parent: Path, name: str) -> Path:
        exact = parent / name
        if exact.exists():
            return exact
        folded = name.casefold()
        match = next((item for item in parent.iterdir() if item.name.casefold() == folded), None)
        if match is None:
            raise FileNotFoundError(parent / name)
        return match

    @classmethod
    def _has_root_files(cls, root: Path) -> bool:
        if not root.is_dir():
            return False
        try:
            return cls._resolve(root, "MANATEE.DRV").is_file() and cls._resolve(root, "ALONE4.MLT").is_file()
        except FileNotFoundError:
            return False

    def _path(self, path: str) -> Path:
        current = self.root
        for component in (part for part in path.replace("\\", "/").split("/") if part):
            current = self._resolve(current, component)
        return current

    def read_file(self, path: str) -> bytes:
        return self._path(path).read_bytes()

    def list_files(self, path: str) -> list[str]:
        directory = self._path(path)
        return sorted((item.name for item in directory.iterdir() if item.is_file()), key=str.casefold)


class IsoDisc(DiscFiles):
    def __init__(self, image: Iso9660):
        self.image = image
        self.image.read_file("MANATEE.DRV")
        self.image.read_file("ALONE4.MLT")

    def close(self) -> None:
        self.image.close()

    def read_file(self, path: str) -> bytes:
        return self.image.read_file(path)

    def list_files(self, path: str) -> list[str]:
        directory = self.image.find(path)
        return sorted((entry.name for entry in self.image.list_directory(directory) if not entry.is_directory), key=str.casefold)


def msf_to_sectors(value: str) -> int:
    minutes, seconds, frames = (int(part) for part in value.split(":"))
    return (minutes * 60 + seconds) * 75 + frames


def cue_data_tracks(path: Path) -> list[TrackSpec]:
    """Turn a Redump-style CUE into global-LBA data-track mappings."""

    blocks: list[dict[str, object]] = []
    high_density = False
    current: dict[str, object] | None = None
    for raw_line in path.read_text(encoding="utf-8-sig", errors="replace").splitlines():
        line = raw_line.strip()
        if re.match(r"^REM\s+HIGH-DENSITY\s+AREA$", line, re.I):
            high_density = True
            continue
        file_match = re.match(r'^FILE\s+(?:"([^"]+)"|(\S+))', line, re.I)
        if file_match:
            current = {
                "path": path.parent / (file_match.group(1) or file_match.group(2)),
                "high_density": high_density,
                "kind": None,
                "index1": 0,
            }
            blocks.append(current)
            continue
        if current is None:
            continue
        track_match = re.match(r"^TRACK\s+\d+\s+(\S+)", line, re.I)
        if track_match:
            current["kind"] = track_match.group(1).upper()
            continue
        index_match = re.match(r"^INDEX\s+01\s+(\d+:\d+:\d+)", line, re.I)
        if index_match:
            current["index1"] = msf_to_sectors(index_match.group(1))

    tracks: list[TrackSpec] = []
    current_lba = 0
    for block in blocks:
        track_path = block["path"]
        assert isinstance(track_path, Path)
        if not track_path.is_file():
            raise FileNotFoundError(track_path)
        if bool(block["high_density"]) and current_lba < 45000:
            current_lba = 45000
        kind = str(block["kind"] or "")
        index1 = int(block["index1"])
        # INDEX 00 pregap sectors are physically present at the start of each
        # Redump BIN but precede the track's published LBA.
        current_lba += index1
        raw_start = index1 * 2352
        sector_size = 2352
        sector_count = max(0, (track_path.stat().st_size - raw_start) // sector_size)
        if kind.startswith("MODE"):
            if kind == "MODE1/2352":
                user_offset = 16
            elif kind == "MODE2/2352":
                user_offset = 24
            elif kind.endswith("/2048"):
                sector_size = 2048
                raw_start = index1 * sector_size
                sector_count = max(0, (track_path.stat().st_size - raw_start) // sector_size)
                user_offset = 0
            else:
                raise ValueError(f"{path}: unsupported CUE data track type {kind}")
            tracks.append(TrackSpec(track_path, current_lba, sector_size, raw_start, user_offset))
        current_lba += sector_count
    return tracks


def gdi_data_tracks(path: Path) -> list[TrackSpec]:
    tracks: list[TrackSpec] = []
    for line in path.read_text(encoding="utf-8-sig", errors="replace").splitlines()[1:]:
        fields = shlex.split(line, posix=False)
        if len(fields) < 6 or fields[2] != "4":
            continue
        filename = fields[4].strip('"')
        sector_size = int(fields[3])
        if sector_size == 2048:
            user_offset = 0
        elif sector_size == 2352:
            user_offset = 16
        elif sector_size == 2336:
            user_offset = 8
        else:
            raise ValueError(f"{path}: unsupported GDI sector size {sector_size}")
        tracks.append(
            TrackSpec(
                path.parent / filename,
                int(fields[1]),
                sector_size,
                int(fields[5]),
                user_offset,
            )
        )
    return tracks


def open_disc(source: Path) -> DiscFiles:
    if source.is_dir():
        return DirectoryDisc(source)
    if not source.is_file():
        raise FileNotFoundError(source)
    suffix = source.suffix.casefold()
    if suffix == ".cue":
        return IsoDisc(GdRomIso(source, cue_data_tracks(source)))
    elif suffix == ".gdi":
        return IsoDisc(GdRomIso(source, gdi_data_tracks(source)))
    elif suffix in (".iso", ".bin", ".img", ".raw"):
        candidates = [source]
    elif suffix in (".chd", ".cdi"):
        raise ValueError(f"{source.suffix} is not read directly; convert it to GDI, BIN/CUE, or ISO first")
    else:
        raise ValueError(f"unsupported image type {source.suffix!r}; use GDI, BIN/CUE, ISO, or an extracted disc directory")

    errors: list[str] = []
    for candidate in reversed(candidates):
        if not candidate.is_file():
            errors.append(f"missing track: {candidate}")
            continue
        try:
            return IsoDisc(Iso9660(candidate))
        except (ValueError, FileNotFoundError) as error:
            errors.append(str(error))
    raise ValueError(f"no Dreamcast data track was usable in {source}: {'; '.join(errors)}")
