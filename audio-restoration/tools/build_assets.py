#!/usr/bin/env python3
"""Build the runtime asset set from a user-supplied Dreamcast disc image.

No game data is shipped with this project.  This tool reads the original files
from an extracted disc, ISO, BIN/CUE, or GDI image and emits only the local
runtime tree consumed by the PC hook.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import shlex
import shutil
import struct
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO, Iterable


EXPECTED_MINIMUM_SCENE_BANKS = 60
ISO_BLOCK_SIZE = 2048
EXECUTABLE_PROFILES = (
    ("retail-cd", 0x97AAD),
    ("15-slot", 0x97C0D),
)
DISPATCH_SIGNATURE = bytes((0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x4C))
PATCH_SECTION = b".aitdaud"
PATCH_MARKER = b"AITDTNN_AUDIO_PATCH_V1\0"
PATCH_HOOK_NAME = "aitd4-audio-hook.dll"
PATCH_BACKUP_SUFFIX = ".aitdtnn-original"
PATCH_METADATA_SUFFIX = ".aitdtnn-patch.json"
PATCH_APPLIED = 0
PATCH_ALREADY_APPLIED = 10

# The PE patcher below is retained only as source-level history and as a tested
# recovery reference for pre-monorepo development builds. It is intentionally
# absent from main(), the packaged asset-builder CLI and the release installer.
# The published architecture always leaves alone4.exe byte-for-byte unchanged.


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def rva_to_file_offset(image: bytes, rva: int) -> int:
    if len(image) < 0x40:
        raise ValueError("not a valid PE executable")
    pe_offset = struct.unpack_from("<I", image, 0x3C)[0]
    if pe_offset + 24 > len(image) or image[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise ValueError("not a valid PE executable")
    section_count = struct.unpack_from("<H", image, pe_offset + 6)[0]
    optional_size = struct.unpack_from("<H", image, pe_offset + 20)[0]
    section_table = pe_offset + 24 + optional_size
    if section_table + section_count * 40 > len(image):
        raise ValueError("truncated PE section table")
    for index in range(section_count):
        section = section_table + index * 40
        virtual_size, virtual_address, raw_size, raw_address = struct.unpack_from("<IIII", image, section + 8)
        if virtual_address <= rva < virtual_address + max(virtual_size, raw_size):
            offset = raw_address + rva - virtual_address
            if offset >= len(image):
                break
            return offset
    raise ValueError(f"RVA 0x{rva:X} is not mapped in the executable")


def detect_executable_profile(path: Path) -> str:
    image = path.read_bytes()
    matches: list[str] = []
    for name, rva in EXECUTABLE_PROFILES:
        try:
            offset = rva_to_file_offset(image, rva)
        except ValueError:
            continue
        if image[offset : offset + len(DISPATCH_SIGNATURE)] == DISPATCH_SIGNATURE:
            matches.append(name)
    if len(matches) != 1:
        raise ValueError(f"{path}: unsupported alone4.exe")
    return matches[0]


def align_up(value: int, alignment: int) -> int:
    if alignment <= 0 or alignment & (alignment - 1):
        raise ValueError(f"invalid PE alignment 0x{alignment:X}")
    return (value + alignment - 1) & ~(alignment - 1)


@dataclass(frozen=True)
class PeSection:
    name: bytes
    virtual_size: int
    virtual_address: int
    raw_size: int
    raw_address: int
    header_offset: int


@dataclass(frozen=True)
class PeLayout:
    pe_offset: int
    optional_offset: int
    section_table: int
    section_count: int
    image_base: int
    entry_rva: int
    section_alignment: int
    file_alignment: int
    size_of_headers: int
    sections: tuple[PeSection, ...]


def parse_pe_layout(image: bytes) -> PeLayout:
    if len(image) < 0x40 or image[:2] != b"MZ":
        raise ValueError("not a valid PE executable")
    pe_offset = struct.unpack_from("<I", image, 0x3C)[0]
    if pe_offset + 24 > len(image) or image[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise ValueError("not a valid PE executable")
    machine, section_count = struct.unpack_from("<HH", image, pe_offset + 4)
    optional_size = struct.unpack_from("<H", image, pe_offset + 20)[0]
    optional_offset = pe_offset + 24
    section_table = optional_offset + optional_size
    if machine != 0x14C or optional_size < 0xE0 or optional_offset + optional_size > len(image):
        raise ValueError("expected a 32-bit x86 PE executable")
    if struct.unpack_from("<H", image, optional_offset)[0] != 0x10B:
        raise ValueError("expected a PE32 optional header")
    if section_table + section_count * 40 > len(image):
        raise ValueError("truncated PE section table")
    sections: list[PeSection] = []
    for index in range(section_count):
        offset = section_table + index * 40
        name = image[offset : offset + 8].rstrip(b"\0")
        virtual_size, virtual_address, raw_size, raw_address = struct.unpack_from("<IIII", image, offset + 8)
        sections.append(PeSection(name, virtual_size, virtual_address, raw_size, raw_address, offset))
    return PeLayout(
        pe_offset=pe_offset,
        optional_offset=optional_offset,
        section_table=section_table,
        section_count=section_count,
        image_base=struct.unpack_from("<I", image, optional_offset + 28)[0],
        entry_rva=struct.unpack_from("<I", image, optional_offset + 16)[0],
        section_alignment=struct.unpack_from("<I", image, optional_offset + 32)[0],
        file_alignment=struct.unpack_from("<I", image, optional_offset + 36)[0],
        size_of_headers=struct.unpack_from("<I", image, optional_offset + 60)[0],
        sections=tuple(sections),
    )


def read_c_string(image: bytes, offset: int) -> str:
    if offset < 0 or offset >= len(image):
        raise ValueError("string offset is outside the PE image")
    end = image.find(b"\0", offset, min(len(image), offset + 512))
    if end < 0:
        raise ValueError("unterminated string in PE image")
    return image[offset:end].decode("ascii", errors="strict")


def find_import_iat_va(image: bytes, dll_name: str, function_name: str) -> int:
    layout = parse_pe_layout(image)
    import_rva, import_size = struct.unpack_from("<II", image, layout.optional_offset + 104)
    if not import_rva or import_size < 20:
        raise ValueError("PE executable has no import table")
    descriptor = rva_to_file_offset(image, import_rva)
    for _ in range(256):
        if descriptor + 20 > len(image):
            break
        original_thunk, timestamp, forwarder, name_rva, first_thunk = struct.unpack_from(
            "<IIIII", image, descriptor
        )
        if not any((original_thunk, timestamp, forwarder, name_rva, first_thunk)):
            break
        imported_dll = read_c_string(image, rva_to_file_offset(image, name_rva))
        thunk_rva = original_thunk or first_thunk
        for index in range(4096):
            thunk_offset = rva_to_file_offset(image, thunk_rva + index * 4)
            thunk_value = struct.unpack_from("<I", image, thunk_offset)[0]
            if thunk_value == 0:
                break
            if thunk_value & 0x80000000:
                continue
            imported_name = read_c_string(image, rva_to_file_offset(image, thunk_value) + 2)
            if imported_dll.lower() == dll_name.lower() and imported_name == function_name:
                return layout.image_base + first_thunk + index * 4
        descriptor += 20
    raise ValueError(f"required import {dll_name}!{function_name} was not found")


def has_audio_patch(image: bytes) -> bool:
    try:
        layout = parse_pe_layout(image)
    except ValueError:
        return False
    for section in layout.sections:
        if section.name == PATCH_SECTION:
            start = section.raw_address
            end = min(len(image), start + section.raw_size)
            return start < end and PATCH_MARKER in image[start:end]
    return False


def build_loader_stub(
    image: bytes, new_section_rva: int, hook_name: str = PATCH_HOOK_NAME
) -> bytes:
    layout = parse_pe_layout(image)
    load_library = find_import_iat_va(image, "KERNEL32.dll", "LoadLibraryA")
    exit_process = find_import_iat_va(image, "KERNEL32.dll", "ExitProcess")
    message_box = find_import_iat_va(image, "USER32.dll", "MessageBoxA")
    hook = hook_name.encode("ascii") + b"\0"
    title = b"AITD:TNN Dreamcast Music Fix\0"
    message = b"The Dreamcast music runtime could not be loaded. Reinstall the patch.\0"
    code_size = 52
    section_va = layout.image_base + new_section_rva
    hook_va = section_va + code_size
    title_va = hook_va + len(hook)
    message_va = title_va + len(title)

    code = bytearray((0x9C, 0x60, 0x68))
    code.extend(struct.pack("<I", hook_va))
    code.extend((0xFF, 0x15))
    code.extend(struct.pack("<I", load_library))
    code.extend((0x85, 0xC0, 0x75, 0x1C, 0x6A, 0x10, 0x68))
    code.extend(struct.pack("<I", title_va))
    code.append(0x68)
    code.extend(struct.pack("<I", message_va))
    code.extend((0x6A, 0x00, 0xFF, 0x15))
    code.extend(struct.pack("<I", message_box))
    code.extend((0x6A, 0x01, 0xFF, 0x15))
    code.extend(struct.pack("<I", exit_process))
    code.extend((0x61, 0x9D, 0xE9))
    jump_from = section_va + len(code) + 4
    jump_to = layout.image_base + layout.entry_rva
    code.extend(struct.pack("<i", jump_to - jump_from))
    if len(code) != code_size:
        raise AssertionError(f"loader stub size changed unexpectedly: {len(code)}")
    return bytes(code) + hook + title + message + PATCH_MARKER


def patch_executable_image(image: bytes) -> bytes:
    if has_audio_patch(image):
        raise ValueError("alone4.exe is already patched")
    layout = parse_pe_layout(image)
    new_header = layout.section_table + layout.section_count * 40
    if new_header + 40 > layout.size_of_headers:
        raise ValueError("PE headers do not have room for the audio loader section")
    highest_virtual_end = max(
        section.virtual_address + max(section.virtual_size, section.raw_size)
        for section in layout.sections
    )
    new_rva = align_up(highest_virtual_end, layout.section_alignment)
    payload = build_loader_stub(image, new_rva)
    new_raw = align_up(len(image), layout.file_alignment)
    new_raw_size = align_up(len(payload), layout.file_alignment)
    patched = bytearray(image)
    patched.extend(b"\0" * (new_raw - len(patched)))
    patched.extend(payload)
    patched.extend(b"\0" * (new_raw_size - len(payload)))
    section_header = struct.pack(
        "<8sIIIIIIHHI",
        PATCH_SECTION,
        len(payload),
        new_rva,
        new_raw_size,
        new_raw,
        0,
        0,
        0,
        0,
        0x60000020,
    )
    patched[new_header : new_header + 40] = section_header
    struct.pack_into("<H", patched, layout.pe_offset + 6, layout.section_count + 1)
    struct.pack_into("<I", patched, layout.optional_offset + 16, new_rva)
    struct.pack_into(
        "<I",
        patched,
        layout.optional_offset + 56,
        align_up(new_rva + len(payload), layout.section_alignment),
    )
    struct.pack_into("<I", patched, layout.optional_offset + 64, 0)
    return bytes(patched)


def executable_patch_paths(executable: Path) -> tuple[Path, Path]:
    return (
        executable.with_name(executable.name + PATCH_BACKUP_SUFFIX),
        executable.with_name(executable.name + PATCH_METADATA_SUFFIX),
    )


def atomic_write(path: Path, data: bytes) -> None:
    temporary = path.with_name(path.name + ".aitdtnn-new")
    try:
        with temporary.open("xb") as handle:
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()


def patch_executable(executable: Path) -> tuple[int, str]:
    executable = executable.resolve()
    image = executable.read_bytes()
    backup, metadata_path = executable_patch_paths(executable)
    if has_audio_patch(image):
        if not backup.is_file() or not metadata_path.is_file():
            raise ValueError("patched alone4.exe is missing its restoration metadata")
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        if metadata.get("patched_sha256") != sha256(image):
            raise ValueError("patched alone4.exe was modified after installation")
        return PATCH_ALREADY_APPLIED, str(metadata.get("profile", "patched"))

    profile = detect_executable_profile(executable)
    original_hash = sha256(image)
    if backup.exists():
        if sha256(backup.read_bytes()) != original_hash:
            raise ValueError(f"refusing to overwrite unrelated backup {backup}")
    else:
        shutil.copy2(executable, backup)
    if metadata_path.exists():
        raise ValueError(f"stale patch metadata exists at {metadata_path}")

    patched = patch_executable_image(image)
    metadata = {
        "format": 1,
        "profile": profile,
        "hook": PATCH_HOOK_NAME,
        "original_sha256": original_hash,
        "patched_sha256": sha256(patched),
    }
    try:
        atomic_write(executable, patched)
        atomic_write(metadata_path, (json.dumps(metadata, indent=2, sort_keys=True) + "\n").encode("utf-8"))
    except Exception:
        shutil.copy2(backup, executable)
        raise
    return PATCH_APPLIED, profile


def unpatch_executable(executable: Path) -> int:
    executable = executable.resolve()
    backup, metadata_path = executable_patch_paths(executable)
    if not backup.is_file():
        if executable.is_file() and not has_audio_patch(executable.read_bytes()):
            return PATCH_ALREADY_APPLIED
        raise ValueError("the original alone4.exe backup is missing")
    backup_image = backup.read_bytes()
    detect_executable_profile(backup)
    if has_audio_patch(backup_image):
        raise ValueError("the alone4.exe backup is itself patched")

    if executable.is_file():
        current = executable.read_bytes()
        if not has_audio_patch(current):
            if sha256(current) == sha256(backup_image):
                backup.unlink()
                if metadata_path.exists():
                    metadata_path.unlink()
                return PATCH_ALREADY_APPLIED
            raise ValueError("alone4.exe is no longer the executable installed by this patch")
        if not metadata_path.is_file():
            raise ValueError("patch metadata is missing; refusing to overwrite alone4.exe")
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        if metadata.get("patched_sha256") != sha256(current):
            raise ValueError("alone4.exe was modified after patch installation; original backup was preserved")
        if metadata.get("original_sha256") != sha256(backup_image):
            raise ValueError("alone4.exe backup does not match the patch metadata")

    temporary = executable.with_name(executable.name + ".aitdtnn-restore")
    try:
        shutil.copy2(backup, temporary)
        os.replace(temporary, executable)
        backup.unlink()
        if metadata_path.exists():
            metadata_path.unlink()
    finally:
        if temporary.exists():
            temporary.unlink()
    return PATCH_APPLIED


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


def parse_midb(data: bytes, container: str) -> tuple[int, list[bytes], list[tuple[str, bytes]], bytes]:
    if len(data) < 7 or data[:4] != b"MIDB":
        raise ValueError(f"{container}: invalid MIDB header")
    bank_id, sequence_count, map_count = data[4:7]
    maps_end = 7 + map_count * 128
    if maps_end > len(data):
        raise ValueError(f"{container}: truncated program maps")
    maps = [data[7 + index * 128 : 7 + (index + 1) * 128] for index in range(map_count)]
    offset = maps_end
    sequences: list[tuple[str, bytes]] = []
    for sequence_index in range(sequence_count):
        if offset + 12 > len(data):
            raise ValueError(f"{container}: truncated sequence header {sequence_index}")
        size = struct.unpack_from("<h", data, offset)[0]
        if size < 12 or offset + 12 + size > len(data):
            raise ValueError(f"{container}: invalid sequence size {size}")
        name = data[offset + 4 : offset + 12].split(b"\0", 1)[0].decode("ascii").lower()
        payload = data[offset + 12 : offset + 12 + size]
        if payload[:4] != b"DSEQ":
            raise ValueError(f"{container}: {name} is not a DSEQ")
        sequences.append((name, payload))
        offset += 12 + size
    bank_offset = data.find(b"SMPB", offset)
    if bank_offset < 0 or bank_offset + 12 > len(data):
        raise ValueError(f"{container}: embedded SMPB bank not found")
    bank_size = struct.unpack_from("<I", data, bank_offset + 8)[0]
    bank = data[bank_offset : bank_offset + bank_size]
    if bank_size < 16 or len(bank) != bank_size or bank[-4:] != b"ENDB":
        raise ValueError(f"{container}: malformed embedded SMPB bank")
    return bank_id, maps, sequences, bank


def write_tsv(path: Path, fieldnames: Iterable[str], rows: list[dict[str, object]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(fieldnames), delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def build_assets(disc: DiscFiles, output: Path, source_label: str) -> dict[str, object]:
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"{output}: output directory is not empty")
    output.mkdir(parents=True, exist_ok=True)
    banks_dir = output / "banks"
    sequences_dir = output / "sequences"
    banks_dir.mkdir(exist_ok=True)
    sequences_dir.mkdir(exist_ok=True)

    driver = disc.read_file("MANATEE.DRV")
    allocation = disc.read_file("ALONE4.MLT")
    if len(driver) < 0x20 or driver[:4] != b"SDRV":
        raise ValueError("MANATEE.DRV has an invalid SDRV header")
    if len(allocation) < 0x20 or allocation[:4] != b"SMLT":
        raise ValueError("ALONE4.MLT has an invalid SMLT header")
    (output / "MANATEE.DRV").write_bytes(driver)
    (output / "ALONE4.MLT").write_bytes(allocation)

    scene_names = disc.list_files("MIDI/ALINE")
    if len(scene_names) < EXPECTED_MINIMUM_SCENE_BANKS:
        raise ValueError(f"Dreamcast MIDI/ALINE contains only {len(scene_names)} files")

    bank_rows: list[dict[str, object]] = []
    map_rows: list[dict[str, object]] = []
    sequence_rows: list[dict[str, object]] = []
    bank_hashes: dict[str, str] = {}
    for source_name in scene_names:
        container = source_name.lower()
        packed = disc.read_file(f"MIDI/ALINE/{source_name}")
        decoded = decompress_padded_gzip(packed, f"MIDI/ALINE/{source_name}")
        bank_id, maps, sequences, bank = parse_midb(decoded, container)
        bank_path = banks_dir / f"{container}.mpb"
        bank_path.write_bytes(bank)
        bank_hashes[container] = sha256(bank)
        bank_rows.append({"container": container, "bank_id": bank_id, "map_count": len(maps)})
        for map_index, voice_map in enumerate(maps):
            for global_voice, local_program in enumerate(voice_map):
                if local_program != 0xFF:
                    map_rows.append({
                        "container": container,
                        "map_index": map_index,
                        "global_voice": global_voice,
                        "local_program": local_program,
                    })
        for sequence_index, (name, payload) in enumerate(sequences):
            filename = f"{container}__{sequence_index:02d}__{name}.dseq"
            (sequences_dir / filename).write_bytes(payload)
            sequence_rows.append({
                "container": container,
                "sequence_index": sequence_index,
                "sequence_name": name,
                "size": len(payload),
                "sha256": sha256(payload),
                "file": f"sequences/{filename}",
            })

    gamesnd_decoded = decompress_padded_gzip(disc.read_file("MENU/GAMESND"), "MENU/GAMESND")
    gamesnd_offset = gamesnd_decoded.find(b"SMPB")
    if gamesnd_offset < 0 or gamesnd_offset + 12 > len(gamesnd_decoded):
        raise ValueError("MENU/GAMESND: SMPB bank not found")
    gamesnd_size = struct.unpack_from("<I", gamesnd_decoded, gamesnd_offset + 8)[0]
    gamesnd = gamesnd_decoded[gamesnd_offset : gamesnd_offset + gamesnd_size]
    if len(gamesnd) != gamesnd_size or gamesnd[-4:] != b"ENDB":
        raise ValueError("MENU/GAMESND: malformed SMPB bank")
    (banks_dir / "gamesnd.mpb").write_bytes(gamesnd)
    bank_hashes["gamesnd"] = sha256(gamesnd)

    write_tsv(output / "banks.tsv", ("container", "bank_id", "map_count"), bank_rows)
    write_tsv(output / "maps.tsv", ("container", "map_index", "global_voice", "local_program"), map_rows)
    write_tsv(
        output / "sequences.tsv",
        ("container", "sequence_index", "sequence_name", "size", "sha256", "file"),
        sequence_rows,
    )

    manifest: dict[str, object] = {
        "format": 1,
        "source": Path(source_label).name,
        "driver_sha256": sha256(driver),
        "allocation_sha256": sha256(allocation),
        "scene_banks": len(bank_rows),
        "bank_files": len(bank_rows) + 1,
        "program_map_entries": len(map_rows),
        "sequences": len(sequence_rows),
        "bank_sha256": bank_hashes,
    }
    (output / "asset-manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description="Build AITD:TNN PC Music Fix assets from an owned Dreamcast disc")
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
