"""Minimal PE parser used only by the tested legacy recovery patcher."""
from __future__ import annotations
import hashlib
import struct
from dataclasses import dataclass
from pathlib import Path

# These two signatures identify the only PC executables for which the recovery
# patcher has a proven dispatch address. Production loading performs its own
# stricter whole-file hash check in the native version proxy.
EXECUTABLE_PROFILES = (
    ("retail-cd", 0x97AAD),
    ("15-slot", 0x97C0D),
)
DISPATCH_SIGNATURE = bytes((0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x4C))

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
