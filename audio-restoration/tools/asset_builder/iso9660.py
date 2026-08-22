"""Small read-only ISO-9660 and Dreamcast GD-ROM filesystem readers."""
from __future__ import annotations
import struct
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO

# ISO-9660 logical sectors are always 2048 user bytes. Dreamcast raw tracks may
# wrap those bytes in larger physical sectors; Iso9660 discovers that layout.
ISO_BLOCK_SIZE = 2048

def decompress_padded_gzip(data: bytes, label: str) -> bytes:
    try:
        decoder = zlib.decompressobj(31)
        result = decoder.decompress(data) + decoder.flush()
    except zlib.error as error:
        raise ValueError(f"{label}: not a valid gzip-compressed game resource: {error}") from error
    if not result:
        raise ValueError(f"{label}: decompressed to an empty file")
    return result


@dataclass(frozen=True)
class DirectoryRecord:
    name: str
    extent: int
    size: int
    is_directory: bool


class Iso9660:
    """Small read-only ISO-9660 reader for Dreamcast MODE1 images."""

    def __init__(self, path: Path):
        self.path = path
        self.stream: BinaryIO = path.open("rb")
        self.sector_size = 0
        self.user_offset = 0
        self.volume_base = 0
        self.root: DirectoryRecord | None = None
        self._discover_layout()

    def close(self) -> None:
        self.stream.close()

    def __enter__(self) -> "Iso9660":
        return self

    def __exit__(self, *_: object) -> None:
        self.close()

    def _sector(self, physical_sector: int) -> bytes:
        if physical_sector < 0:
            raise ValueError(f"{self.path}: invalid negative sector")
        self.stream.seek(physical_sector * self.sector_size + self.user_offset)
        data = self.stream.read(ISO_BLOCK_SIZE)
        if len(data) != ISO_BLOCK_SIZE:
            raise ValueError(f"{self.path}: truncated sector {physical_sector}")
        return data

    def _discover_layout(self) -> None:
        file_size = self.path.stat().st_size
        layouts = ((2048, 0), (2352, 16), (2352, 24), (2336, 8))
        for sector_size, user_offset in layouts:
            maximum = min(2048, file_size // sector_size)
            candidates = list(range(16, maximum))
            total_sectors = file_size // sector_size
            if total_sectors > 45016:
                candidates.extend(range(45016, min(45144, total_sectors)))
            for physical_sector in candidates:
                self.stream.seek(physical_sector * sector_size + user_offset)
                header = self.stream.read(7)
                if header != b"\x01CD001\x01":
                    continue
                self.sector_size = sector_size
                self.user_offset = user_offset
                pvd = self._sector(physical_sector)
                self.root = self._parse_record(pvd, 156)
                if not self.root or not self.root.is_directory:
                    raise ValueError(f"{self.path}: invalid ISO root directory")
                # Dreamcast high-density extents use absolute disc LBAs. A
                # session-only ISO may place its LBA-45016 PVD at physical
                # sector 16, while a merged raw image keeps it at 45016.
                self.volume_base = (
                    physical_sector - 45016 if self.root.extent >= 45000
                    else physical_sector - 16
                )
                return
        raise ValueError(f"{self.path}: no ISO-9660 primary volume descriptor found")

    @staticmethod
    def _parse_record(data: bytes, offset: int) -> DirectoryRecord | None:
        if offset >= len(data) or data[offset] == 0:
            return None
        length = data[offset]
        if length < 34 or offset + length > len(data):
            raise ValueError("malformed ISO-9660 directory record")
        extent = struct.unpack_from("<I", data, offset + 2)[0]
        size = struct.unpack_from("<I", data, offset + 10)[0]
        flags = data[offset + 25]
        name_length = data[offset + 32]
        raw_name = data[offset + 33 : offset + 33 + name_length]
        if raw_name == b"\x00":
            name = "."
        elif raw_name == b"\x01":
            name = ".."
        else:
            # Some GD-ROM authoring tools encode extensionless names as
            # ``NAME.;1``. Present those to callers as the intended ``NAME``.
            name = raw_name.decode("ascii", errors="strict").split(";", 1)[0].rstrip(".")
        return DirectoryRecord(name, extent, size, bool(flags & 2))

    def read_extent(self, extent: int, size: int) -> bytes:
        output = bytearray()
        blocks = (size + ISO_BLOCK_SIZE - 1) // ISO_BLOCK_SIZE
        for block in range(blocks):
            output.extend(self._sector(self.volume_base + extent + block))
        return bytes(output[:size])

    def list_directory(self, record: DirectoryRecord) -> list[DirectoryRecord]:
        if not record.is_directory:
            raise ValueError(f"{record.name}: not a directory")
        data = self.read_extent(record.extent, record.size)
        records: list[DirectoryRecord] = []
        offset = 0
        while offset < len(data):
            length = data[offset]
            if length == 0:
                offset = ((offset // ISO_BLOCK_SIZE) + 1) * ISO_BLOCK_SIZE
                continue
            record_item = self._parse_record(data, offset)
            if record_item and record_item.name not in (".", ".."):
                records.append(record_item)
            offset += length
        return records

    def find(self, path: str) -> DirectoryRecord:
        if self.root is None:
            raise RuntimeError("ISO reader is not initialized")
        current = self.root
        for component in (part for part in path.replace("\\", "/").split("/") if part):
            entries = self.list_directory(current)
            match = next((entry for entry in entries if entry.name.casefold() == component.casefold()), None)
            if match is None:
                raise FileNotFoundError(f"{self.path}: /{path} not found")
            current = match
        return current

    def read_file(self, path: str) -> bytes:
        record = self.find(path)
        if record.is_directory:
            raise IsADirectoryError(path)
        return self.read_extent(record.extent, record.size)


@dataclass(frozen=True)
class TrackSpec:
    path: Path
    start_lba: int
    sector_size: int
    raw_start: int = 0
    user_offset: int = 16

    @property
    def sector_count(self) -> int:
        available = self.path.stat().st_size - self.raw_start
        return max(0, available // self.sector_size)


class GdRomIso(Iso9660):
    """ISO-9660 view over the split high-density tracks of a GD-ROM."""

    def __init__(self, label: Path, tracks: list[TrackSpec]):
        self.path = label
        self.tracks = sorted(tracks, key=lambda track: track.start_lba)
        self.streams: dict[Path, BinaryIO] = {}
        self.sector_size = ISO_BLOCK_SIZE
        self.user_offset = 0
        # GD-ROM directory extents are absolute disc LBAs, not relative to
        # the high-density session that starts at LBA 45000.
        self.volume_base = 0
        self.root: DirectoryRecord | None = None
        if not self.tracks:
            raise ValueError(f"{label}: no data tracks")
        for track in self.tracks:
            if not track.path.is_file():
                raise FileNotFoundError(track.path)
            self.streams[track.path] = track.path.open("rb")
        try:
            self._discover_gdrom_layout()
        except Exception:
            self.close()
            raise

    def close(self) -> None:
        for stream in self.streams.values():
            stream.close()
        self.streams.clear()

    def _track_for_lba(self, lba: int) -> TrackSpec | None:
        for track in reversed(self.tracks):
            if track.start_lba <= lba < track.start_lba + track.sector_count:
                return track
        return None

    def _sector(self, physical_sector: int) -> bytes:
        track = self._track_for_lba(physical_sector)
        if track is None:
            raise ValueError(f"{self.path}: LBA {physical_sector} is outside the supplied data tracks")
        local_sector = physical_sector - track.start_lba
        stream = self.streams[track.path]
        stream.seek(track.raw_start + local_sector * track.sector_size + track.user_offset)
        data = stream.read(ISO_BLOCK_SIZE)
        if len(data) != ISO_BLOCK_SIZE:
            raise ValueError(f"{track.path}: truncated sector at LBA {physical_sector}")
        return data

    def _discover_gdrom_layout(self) -> None:
        # The Dreamcast high-density session begins at 45000 and its PVD is
        # normally at 45016. Scan a little farther to accept equivalent dumps.
        scan_starts = [track.start_lba for track in self.tracks if track.start_lba >= 45000]
        for start_lba in scan_starts:
            for lba in range(start_lba + 16, start_lba + 128):
                try:
                    pvd = self._sector(lba)
                except ValueError:
                    break
                if pvd[:7] != b"\x01CD001\x01":
                    continue
                self.root = self._parse_record(pvd, 156)
                if not self.root or not self.root.is_directory:
                    raise ValueError(f"{self.path}: invalid GD-ROM ISO root directory")
                return
        raise ValueError(f"{self.path}: no high-density ISO-9660 primary volume descriptor found")
